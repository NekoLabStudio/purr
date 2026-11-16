//! Watches the local network for an Apple Vision Pro, so the game asks macOS to show it there
//! only when one is around (the runtime's vision_pro.mm). Apple's devices announce themselves
//! over Bonjour as `_companion-link._tcp` services, with their model in the TXT record's `rpMd`
//! (a Vision Pro's starts with `RealityDevice`). Reports `{"event":"vision_pro","nearby":<bool>}`
//! at the first look and whenever that changes.

use std::{
    ffi::{c_char, c_void, CStr},
    sync::atomic::{AtomicBool, Ordering},
    time::{Duration, Instant},
};

type ServiceRef = *mut c_void;
type BrowseReply = extern "C" fn(ServiceRef, u32, u32, i32, *const c_char, *const c_char, *const c_char, *mut c_void);
type ResolveReply = extern "C" fn(ServiceRef, u32, u32, i32, *const c_char, *const c_char, u16, u16, *const u8, *mut c_void);

// dns_sd.h, in libSystem.
extern "C" {
    fn DNSServiceBrowse(service: *mut ServiceRef, flags: u32, interface: u32, kind: *const c_char, domain: *const c_char, reply: BrowseReply, context: *mut c_void) -> i32;
    #[allow(clippy::too_many_arguments)]
    fn DNSServiceResolve(service: *mut ServiceRef, flags: u32, interface: u32, name: *const c_char, kind: *const c_char, domain: *const c_char, reply: ResolveReply, context: *mut c_void) -> i32;
    fn DNSServiceRefSockFD(service: ServiceRef) -> i32;
    fn DNSServiceProcessResult(service: ServiceRef) -> i32;
    fn DNSServiceRefDeallocate(service: ServiceRef);
    fn TXTRecordGetValuePtr(length: u16, record: *const c_void, key: *const c_char, value_length: *mut u8) -> *const c_void;
}

const SERVICE: &CStr = c"_companion-link._tcp";
const DOMAIN: &CStr = c"local.";
const FLAG_ADD: u32 = 0x2;
const MODEL: &[u8] = b"RealityDevice";
/// How long each look listens, and how often it looks.
const LISTEN: Duration = Duration::from_millis(1500);
const EVERY: Duration = Duration::from_secs(5);

struct Found {
    services: Vec<(String, u32)>,
}

extern "C" fn on_browse(_: ServiceRef, flags: u32, interface: u32, error: i32, name: *const c_char, _: *const c_char, _: *const c_char, context: *mut c_void) {
    if error != 0 || flags & FLAG_ADD == 0 || name.is_null() {
        return;
    }
    let found = unsafe { &mut *(context as *mut Found) };
    let name = unsafe { CStr::from_ptr(name) }.to_string_lossy().into_owned();
    found.services.push((name, interface));
}

extern "C" fn on_resolve(_: ServiceRef, _: u32, _: u32, error: i32, _: *const c_char, _: *const c_char, _: u16, length: u16, record: *const u8, context: *mut c_void) {
    if error != 0 || record.is_null() {
        return;
    }
    let mut value_length = 0u8;
    let value = unsafe { TXTRecordGetValuePtr(length, record as *const c_void, c"rpMd".as_ptr(), &mut value_length) };
    if !value.is_null() {
        let model = unsafe { std::slice::from_raw_parts(value as *const u8, value_length as usize) };
        if model.starts_with(MODEL) {
            unsafe { *(context as *mut bool) = true };
        }
    }
}

/// Hands `service`'s replies to their callbacks until `until`, or until `done` says so.
fn process_until(service: ServiceRef, until: Instant, done: impl Fn() -> bool) {
    let fd = unsafe { DNSServiceRefSockFD(service) };
    while !done() {
        let Some(left) = until.checked_duration_since(Instant::now()) else { return };
        let mut poll = libc::pollfd { fd, events: libc::POLLIN, revents: 0 };
        let ready = unsafe { libc::poll(&mut poll, 1, left.as_millis().max(1) as i32) };
        if ready <= 0 || unsafe { DNSServiceProcessResult(service) } != 0 {
            return;
        }
    }
}

/// Whether a Vision Pro announces itself now.
fn look() -> bool {
    let mut found = Found { services: Vec::new() };
    let mut browse: ServiceRef = std::ptr::null_mut();
    let context = &mut found as *mut Found as *mut c_void;
    if unsafe { DNSServiceBrowse(&mut browse, 0, 0, SERVICE.as_ptr(), DOMAIN.as_ptr(), on_browse, context) } != 0 {
        return false;
    }
    process_until(browse, Instant::now() + LISTEN, || false);
    unsafe { DNSServiceRefDeallocate(browse) };

    found.services.iter().any(|(name, interface)| {
        let Ok(name) = std::ffi::CString::new(name.as_str()) else { return false };
        let mut vision_pro = false;
        let mut resolve: ServiceRef = std::ptr::null_mut();
        let context = &mut vision_pro as *mut bool as *mut c_void;
        if unsafe { DNSServiceResolve(&mut resolve, 0, *interface, name.as_ptr(), SERVICE.as_ptr(), DOMAIN.as_ptr(), on_resolve, context) } != 0 {
            return false;
        }
        let answered = AtomicBool::new(false);
        process_until(resolve, Instant::now() + Duration::from_secs(1), || {
            answered.swap(true, Ordering::Relaxed) // one reply is enough
        });
        unsafe { DNSServiceRefDeallocate(resolve) };
        vision_pro
    })
}

/// Looks every few seconds from a thread of its own, for as long as the streamer runs.
pub fn watch() {
    std::thread::Builder::new()
        .name("vision-pro-watch".into())
        .spawn(|| {
            let mut last: Option<bool> = None;
            loop {
                let nearby = look();
                if last != Some(nearby) {
                    if nearby {
                        log::info!("a Vision Pro is nearby");
                    }
                    crate::status::status("vision_pro", serde_json::json!({ "nearby": nearby }));
                    last = Some(nearby);
                }
                std::thread::sleep(EVERY);
            }
        })
        .ok();
}
