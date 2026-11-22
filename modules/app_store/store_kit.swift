// StoreKit 2 for Purr's Store on Apple platforms: what the player owns, prices, purchases,
// restoring, and the AppTransaction that proves to Purr's online service the app came from the
// App Store. StoreKit 2 is Swift-only; app_store.mm calls this through the Objective-C interface
// swiftc writes (purr_app_store-Swift.gen.h). Every handler is called on the main thread. StoreKit 2
// needs macOS 12, iOS 15 and tvOS 15, and the AppTransaction macOS 13 and iOS 16: older systems
// have no store here.

import Foundation
import StoreKit

@objc(PurrStoreKit)
public final class PurrStoreKit: NSObject {
	private static var productIDs: [String] = []
	private static var prices: [String: String] = [:]
	private static var changed: (([String], [String: String]) -> Void)?
	private static var updates: Task<Void, Never>?

	// Only an App Store or TestFlight copy can buy anything. On a Mac, a copy from elsewhere has no
	// receipt, and asking StoreKit there would ask the player to sign in to the App Store.
	@objc public static func isAvailable() -> Bool {
		guard #available(macOS 12.0, iOS 15.0, tvOS 15.0, *) else {
			return false
		}
		#if os(macOS)
		guard let receipt = Bundle.main.appStoreReceiptURL, FileManager.default.fileExists(atPath: receipt.path) else {
			return false
		}
		#endif
		return AppStore.canMakePayments
	}

	// Loads what the player owns and the products' prices, then calls handler, and again whenever
	// either changes (a purchase on another device, a refund, Ask to Buy approved).
	@objc public static func start(_ ids: [String], changed handler: @escaping ([String], [String: String]) -> Void) {
		guard #available(macOS 12.0, iOS 15.0, tvOS 15.0, *) else {
			return
		}
		productIDs = ids
		changed = handler
		Task { await refresh() }
		updates?.cancel()
		updates = Task.detached {
			for await update in Transaction.updates {
				if case .verified(let transaction) = update {
					await transaction.finish()
				}
				await refresh()
			}
		}
	}

	@available(macOS 12.0, iOS 15.0, tvOS 15.0, *)
	private static func owned() async -> [String] {
		var ids: [String] = []
		for await entitlement in Transaction.currentEntitlements {
			if case .verified(let transaction) = entitlement, transaction.revocationDate == nil, productIDs.contains(transaction.productID) {
				ids.append(transaction.productID)
			}
		}
		return ids
	}

	@available(macOS 12.0, iOS 15.0, tvOS 15.0, *)
	private static func refresh() async {
		let ids = await owned()
		if prices.isEmpty, let products = try? await Product.products(for: productIDs) {
			for product in products {
				prices[product.id] = product.displayPrice
			}
		}
		let known = prices
		await MainActor.run { changed?(ids, known) }
	}

	private static func finish(_ done: @escaping (String, String) -> Void, _ error: String, _ message: String) async {
		await MainActor.run { done(error, message) }
	}

	// Calls done with an empty error when bought, or "cancelled", "pending", "unavailable" or
	// "failed" and a message.
	@objc public static func purchase(_ id: String, done: @escaping (String, String) -> Void) {
		guard #available(macOS 12.0, iOS 15.0, tvOS 15.0, *) else {
			done("unavailable", "The App Store needs a newer system here.")
			return
		}
		Task {
			do {
				guard let product = try await Product.products(for: [id]).first else {
					await finish(done, "unavailable", "The App Store doesn't sell \(id).")
					return
				}
				#if os(visionOS)
				guard let scene = await UIApplication.shared.connectedScenes.first else {
					await finish(done, "failed", "There's no window to confirm the purchase in.")
					return
				}
				let result = try await product.purchase(confirmIn: scene)
				#else
				let result = try await product.purchase()
				#endif
				switch result {
				case .success(let verification):
					guard case .verified(let transaction) = verification else {
						await finish(done, "failed", "The App Store couldn't verify the purchase.")
						return
					}
					await transaction.finish()
					await refresh()
					await finish(done, "", "")
				case .userCancelled:
					await finish(done, "cancelled", "The player cancelled the purchase.")
				case .pending:
					await finish(done, "pending", "The purchase is waiting for approval (Ask to Buy, say).")
				@unknown default:
					await finish(done, "failed", "The App Store gave an answer this version of Purr doesn't know.")
				}
			} catch {
				await finish(done, "failed", error.localizedDescription)
			}
		}
	}

	@objc public static func restore(_ done: @escaping (String, String) -> Void) {
		guard #available(macOS 12.0, iOS 15.0, tvOS 15.0, *) else {
			done("unavailable", "The App Store needs a newer system here.")
			return
		}
		Task {
			do {
				try await AppStore.sync()
				await refresh()
				await finish(done, "", "")
			} catch StoreKitError.userCancelled {
				await finish(done, "cancelled", "The player cancelled signing in to the App Store.")
			} catch {
				await finish(done, "failed", error.localizedDescription)
			}
		}
	}

	// The App Store's signed AppTransaction (a JWS), or an empty string when there's none.
	@objc public static func appTransaction(_ done: @escaping (String) -> Void) {
		guard #available(macOS 13.0, iOS 16.0, tvOS 16.0, *) else {
			done("")
			return
		}
		Task {
			let jws = (try? await AppTransaction.shared)?.jwsRepresentation ?? ""
			await MainActor.run { done(jws) }
		}
	}
}
