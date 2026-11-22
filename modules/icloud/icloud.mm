#include "icloud.h"

#include "core/config/project_settings.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

#import <CloudKit/CloudKit.h>
#import <Foundation/Foundation.h>
#if defined(MACOS_ENABLED)
#import <Security/Security.h>
#endif

ICloud *ICloud::singleton = nullptr;

void ICloudCall::_bind_methods() {
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::DICTIONARY, "result")));
}

namespace {

NSString *const RECORD_TYPE = @"PurrFile";
NSString *const ZONE_NAME = @"Purr";
// Set once the zone is known to exist (creating it again is harmless, just slower).
bool zone_ready = false;

NSString *ns_string(const String &p_string) {
	return [NSString stringWithUTF8String:p_string.utf8().get_data()];
}

String godot_string(NSString *p_string) {
	return p_string ? String::utf8(p_string.UTF8String) : String();
}

Dictionary result(NSError *p_error) {
	Dictionary dictionary;
	dictionary["ok"] = p_error == nil;
	if (p_error) {
		// A short code, and CloudKit's own message.
		const bool cloudkit = [p_error.domain isEqualToString:CKErrorDomain];
		dictionary["error"] = cloudkit && p_error.code == CKErrorNotAuthenticated ? "not_signed_in" : cloudkit && (p_error.code == CKErrorNetworkUnavailable || p_error.code == CKErrorNetworkFailure) ? "offline" : cloudkit && p_error.code == CKErrorUnknownItem ? "not_found" : "failed";
		dictionary["message"] = godot_string(p_error.localizedDescription);
	}
	return dictionary;
}

// Milliseconds since 1970: when the record was last saved, as iCloud's servers saw it.
int64_t modified(CKRecord *p_record) {
	return (int64_t)(p_record.modificationDate.timeIntervalSince1970 * 1000.0);
}

// CloudKit's handlers run on any thread: results go back to Godot's main thread.
void finish(const Ref<ICloudCall> &p_call, const Dictionary &p_result) {
	callable_mp(ICloud::get_singleton(), &ICloud::finish_call).call_deferred(p_call, p_result);
}

Dictionary parse_metadata(NSString *p_json) {
	const Variant parsed = JSON::parse_string(godot_string(p_json));
	return parsed.get_type() == Variant::DICTIONARY ? Dictionary(parsed) : Dictionary();
}

Dictionary metadata_of(CKRecord *p_record) {
	return parse_metadata(p_record[@"metadata"]);
}

// The iCloud containers the game's entitlements give it. CloudKit ends the process when asked for
// a container the game hasn't been given: a game run from the editor, or exported without them.
// Apple's other platforms can't read their own entitlements; the export plugin checks them there.
bool has_container_entitlement(const String &p_container) {
#if defined(MACOS_ENABLED)
	static bool read = false;
	static PackedStringArray containers;
	if (!read) {
		read = true;
		SecTaskRef task = SecTaskCreateFromSelf(kCFAllocatorDefault);
		if (task != nullptr) {
			CFTypeRef value = SecTaskCopyValueForEntitlement(task, CFSTR("com.apple.developer.icloud-container-identifiers"), nullptr);
			if (value != nullptr && CFGetTypeID(value) == CFArrayGetTypeID()) {
				for (CFIndex i = 0; i < CFArrayGetCount((CFArrayRef)value); i++) {
					CFTypeRef entry = CFArrayGetValueAtIndex((CFArrayRef)value, i);
					if (CFGetTypeID(entry) == CFStringGetTypeID()) {
						containers.push_back(godot_string((__bridge NSString *)entry));
					}
				}
			}
			if (value != nullptr) {
				CFRelease(value);
			}
			CFRelease(task);
		}
	}
	return containers.has(p_container);
#else
	return true;
#endif
}

CKDatabase *database() {
	return [CKContainer containerWithIdentifier:ns_string(ICloud::get_singleton()->get_container())].privateCloudDatabase;
}

CKRecordZoneID *zone_id() {
	return [[CKRecordZoneID alloc] initWithZoneName:ZONE_NAME ownerName:CKCurrentUserDefaultName];
}

CKRecordID *record_id(const String &p_name) {
	return [[CKRecordID alloc] initWithRecordName:ns_string(p_name) zoneID:zone_id()];
}

// Runs p_then once Purr's zone exists in the player's private database.
void with_zone(void (^p_then)(NSError *)) {
	if (zone_ready) {
		p_then(nil);
		return;
	}
	CKModifyRecordZonesOperation *operation = [[CKModifyRecordZonesOperation alloc] initWithRecordZonesToSave:@[ [[CKRecordZone alloc] initWithZoneID:zone_id()] ] recordZoneIDsToDelete:nil];
	operation.modifyRecordZonesCompletionBlock = ^(NSArray<CKRecordZone *> *saved, NSArray<CKRecordZoneID *> *deleted, NSError *error) {
		if (!error) {
			zone_ready = true;
		}
		p_then(error);
	};
	[database() addOperation:operation];
}

} // namespace

ICloud::ICloud() {
	singleton = this;
	GLOBAL_DEF_BASIC(PropertyInfo(Variant::STRING, "icloud/container", PROPERTY_HINT_PLACEHOLDER_TEXT, "iCloud.com.example.game"), "");
}

ICloud::~ICloud() {
	singleton = nullptr;
}

Ref<ICloudCall> ICloud::begin_call() {
	Ref<ICloudCall> call;
	call.instantiate();
	pending.insert(call);
	return call;
}

void ICloud::finish_call(const Ref<ICloudCall> &p_call, const Dictionary &p_result) {
	p_call->emit_signal(SNAME("completed"), p_result);
	pending.erase(p_call);
}

Signal ICloud::fail_now(const String &p_error, const String &p_message) {
	const Ref<ICloudCall> call = begin_call();
	Dictionary failed;
	failed["ok"] = false;
	failed["error"] = p_error;
	failed["message"] = p_message;
	callable_mp(this, &ICloud::finish_call).call_deferred(call, failed);
	return Signal(call.ptr(), SNAME("completed"));
}

String ICloud::get_container() const {
	return String(GLOBAL_GET("icloud/container")).strip_edges();
}

bool ICloud::is_available() const {
	// Set up for this game, given the container by its entitlements, and the player signed in to
	// iCloud on this device.
	const String container = get_container();
	return !container.is_empty() && has_container_entitlement(container) && NSFileManager.defaultManager.ubiquityIdentityToken != nil;
}

Signal ICloud::save_file(const String &p_name, const PackedByteArray &p_data, const Dictionary &p_metadata) {
	if (!is_available()) {
		return fail_now("unavailable", "iCloud isn't available: no container, or the player isn't signed in.");
	}
	// CloudKit uploads an asset from a file, so the data goes to a temporary one first.
	NSURL *file = [NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
	NSData *data = [NSData dataWithBytes:p_data.ptr() length:p_data.size()];
	if (![data writeToURL:file atomically:YES]) {
		return fail_now("failed", "Couldn't write the file for iCloud.");
	}
	const Ref<ICloudCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	CKRecordID *record = record_id(p_name);
	NSString *metadata = ns_string(JSON::stringify(p_metadata));
	with_zone(^(NSError *zone_error) {
		if (zone_error) {
			[NSFileManager.defaultManager removeItemAtURL:file error:nil];
			finish(call, result(zone_error));
			return;
		}
		CKRecord *saving = [[CKRecord alloc] initWithRecordType:RECORD_TYPE recordID:record];
		saving[@"data"] = [[CKAsset alloc] initWithFileURL:file];
		saving[@"metadata"] = metadata;
		CKModifyRecordsOperation *operation = [[CKModifyRecordsOperation alloc] initWithRecordsToSave:@[ saving ] recordIDsToDelete:nil];
		// This device's file replaces iCloud's: PlayerSaves decides beforehand which one wins.
		operation.savePolicy = CKRecordSaveAllKeys;
		operation.modifyRecordsCompletionBlock = ^(NSArray<CKRecord *> *saved, NSArray<CKRecordID *> *deleted, NSError *error) {
			[NSFileManager.defaultManager removeItemAtURL:file error:nil];
			Dictionary done = result(error);
			done["modified"] = saved.firstObject ? modified(saved.firstObject) : (int64_t)0;
			finish(call, done);
		};
		[database() addOperation:operation];
	});
	return completed;
}

Signal ICloud::load_file(const String &p_name) {
	if (!is_available()) {
		return fail_now("unavailable", "iCloud isn't available: no container, or the player isn't signed in.");
	}
	const Ref<ICloudCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	[database() fetchRecordWithID:record_id(p_name)
				completionHandler:^(CKRecord *record, NSError *error) {
					Dictionary done = result(error);
					if (record) {
						CKAsset *asset = record[@"data"];
						NSData *data = asset.fileURL ? [NSData dataWithContentsOfURL:asset.fileURL] : nil;
						PackedByteArray bytes;
						bytes.resize(data.length);
						if (data.length > 0) {
							memcpy(bytes.ptrw(), data.bytes, data.length);
						}
						done["data"] = bytes;
						done["modified"] = modified(record);
						done["metadata"] = metadata_of(record);
					}
					finish(call, done);
				}];
	return completed;
}

// Every file's name, metadata and when it was last saved, without downloading any of them.
Signal ICloud::list_files() {
	if (!is_available()) {
		return fail_now("unavailable", "iCloud isn't available: no container, or the player isn't signed in.");
	}
	const Ref<ICloudCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	with_zone(^(NSError *zone_error) {
		if (zone_error) {
			finish(call, result(zone_error));
			return;
		}
		CKFetchRecordZoneChangesConfiguration *configuration = [CKFetchRecordZoneChangesConfiguration new];
		configuration.desiredKeys = @[ @"metadata" ];
		CKFetchRecordZoneChangesOperation *operation = [[CKFetchRecordZoneChangesOperation alloc] initWithRecordZoneIDs:@[ zone_id() ] configurationsByRecordZoneID:@{ zone_id() : configuration }];
		NSMutableArray<NSDictionary *> *found = [NSMutableArray array];
		operation.recordWasChangedBlock = ^(CKRecordID *record_id, CKRecord *record, NSError *error) {
			if (record) {
				@synchronized(found) {
					[found addObject:@{ @"name" : record_id.recordName, @"modified" : @(modified(record)), @"metadata" : record[@"metadata"] ?: @"{}" }];
				}
			}
		};
		operation.fetchRecordZoneChangesCompletionBlock = ^(NSError *error) {
			Dictionary done = result(error);
			Array files;
			@synchronized(found) {
				for (NSDictionary *each in found) {
					Dictionary file;
					file["name"] = godot_string(each[@"name"]);
					file["modified"] = (int64_t)[each[@"modified"] longLongValue];
					file["metadata"] = parse_metadata(each[@"metadata"]);
					files.push_back(file);
				}
			}
			done["files"] = files;
			finish(call, done);
		};
		[database() addOperation:operation];
	});
	return completed;
}

Signal ICloud::delete_file(const String &p_name) {
	if (!is_available()) {
		return fail_now("unavailable", "iCloud isn't available: no container, or the player isn't signed in.");
	}
	const Ref<ICloudCall> call = begin_call();
	const Signal completed(call.ptr(), SNAME("completed"));
	CKModifyRecordsOperation *operation = [[CKModifyRecordsOperation alloc] initWithRecordsToSave:nil recordIDsToDelete:@[ record_id(p_name) ]];
	operation.modifyRecordsCompletionBlock = ^(NSArray<CKRecord *> *saved, NSArray<CKRecordID *> *deleted, NSError *error) {
		finish(call, result(error));
	};
	[database() addOperation:operation];
	return completed;
}

void ICloud::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_container"), &ICloud::get_container);
	ClassDB::bind_method(D_METHOD("is_available"), &ICloud::is_available);
	ClassDB::bind_method(D_METHOD("save_file", "name", "data", "metadata"), &ICloud::save_file, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("load_file", "name"), &ICloud::load_file);
	ClassDB::bind_method(D_METHOD("list_files"), &ICloud::list_files);
	ClassDB::bind_method(D_METHOD("delete_file", "name"), &ICloud::delete_file);
}
