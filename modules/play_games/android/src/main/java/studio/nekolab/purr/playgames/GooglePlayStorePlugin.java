package studio.nekolab.purr.playgames;

import android.app.Activity;
import android.content.pm.PackageManager;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.android.billingclient.api.AcknowledgePurchaseParams;
import com.android.billingclient.api.BillingClient;
import com.android.billingclient.api.BillingClientStateListener;
import com.android.billingclient.api.BillingFlowParams;
import com.android.billingclient.api.BillingResult;
import com.android.billingclient.api.PendingPurchasesParams;
import com.android.billingclient.api.ProductDetails;
import com.android.billingclient.api.Purchase;
import com.android.billingclient.api.QueryProductDetailsParams;
import com.android.billingclient.api.QueryPurchasesParams;
import com.google.android.play.core.integrity.IntegrityManagerFactory;
import com.google.android.play.core.integrity.StandardIntegrityManager;

import org.godotengine.godot.Dictionary;
import org.godotengine.godot.Godot;
import org.godotengine.godot.plugin.GodotPlugin;
import org.godotengine.godot.plugin.SignalInfo;
import org.godotengine.godot.plugin.UsedByGodot;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The Android side of Purr's Store on Google Play: Play Billing for one-time products the player
 * keeps (DLC, expansions), and Play Integrity for proving to Purr's online service that the game
 * came from Google Play. Asynchronous methods take a call ID from the C++ side and answer with the
 * "finished" signal (the ID and a Dictionary with "ok", and "error" and "message" when it failed);
 * what the player owns and the prices arrive with "changed".
 */
public class GooglePlayStorePlugin extends GodotPlugin {
	private static final SignalInfo FINISHED = new SignalInfo("finished", Integer.class, Dictionary.class);
	private static final SignalInfo CHANGED = new SignalInfo("changed", Dictionary.class, Dictionary.class);

	private BillingClient billing;
	private final List<String> productIds = new ArrayList<>();
	private final Map<String, ProductDetails> products = new HashMap<>();
	private final Map<String, String> prices = new HashMap<>();
	private final Set<String> owned = new HashSet<>();
	private int purchaseCallId = -1; // The purchase under way, answered from the purchases listener.
	private StandardIntegrityManager.StandardIntegrityTokenProvider integrity;

	public GooglePlayStorePlugin(Godot godot) {
		super(godot);
	}

	@NonNull
	@Override
	public String getPluginName() {
		return "PurrStoreAndroid";
	}

	@NonNull
	@Override
	public Set<SignalInfo> getPluginSignals() {
		Set<SignalInfo> signals = new HashSet<>();
		signals.add(FINISHED);
		signals.add(CHANGED);
		return signals;
	}

	private void finish(int callId, @Nullable String error, @Nullable String message) {
		Dictionary result = new Dictionary();
		result.put("ok", error == null);
		if (error != null) {
			result.put("error", error);
			result.put("message", message != null ? message : error);
		}
		emitSignal(FINISHED.getName(), callId, result);
	}

	private void changed() {
		Dictionary nowOwned = new Dictionary();
		for (String id : owned) {
			nowOwned.put(id, true);
		}
		Dictionary nowPrices = new Dictionary();
		for (Map.Entry<String, String> price : prices.entrySet()) {
			nowPrices.put(price.getKey(), price.getValue());
		}
		emitSignal(CHANGED.getName(), nowOwned, nowPrices);
	}

	// Only a device with Google Play's store can buy anything.
	@UsedByGodot
	public boolean isAvailable() {
		Activity activity = getActivity();
		if (activity == null) {
			return false;
		}
		try {
			activity.getPackageManager().getPackageInfo("com.android.vending", 0);
			return true;
		} catch (PackageManager.NameNotFoundException e) {
			return false;
		}
	}

	// Connects to Play Billing and loads the products' prices and what the player owns.
	@UsedByGodot
	public void start(String[] ids) {
		Activity activity = getActivity();
		if (activity == null || billing != null) {
			return;
		}
		productIds.clear();
		for (String id : ids) {
			productIds.add(id);
		}
		billing = BillingClient.newBuilder(activity)
				.setListener(this::purchasesUpdated)
				.enablePendingPurchases(PendingPurchasesParams.newBuilder().enableOneTimeProducts().build())
				.enableAutoServiceReconnection()
				.build();
		billing.startConnection(new BillingClientStateListener() {
			@Override
			public void onBillingSetupFinished(@NonNull BillingResult result) {
				if (result.getResponseCode() == BillingClient.BillingResponseCode.OK) {
					loadProducts();
					loadPurchases(-1);
				}
			}

			@Override
			public void onBillingServiceDisconnected() {
				// Play Billing reconnects on its own (enableAutoServiceReconnection).
			}
		});
	}

	private void loadProducts() {
		if (productIds.isEmpty()) {
			return;
		}
		List<QueryProductDetailsParams.Product> list = new ArrayList<>();
		for (String id : productIds) {
			list.add(QueryProductDetailsParams.Product.newBuilder().setProductId(id).setProductType(BillingClient.ProductType.INAPP).build());
		}
		billing.queryProductDetailsAsync(QueryProductDetailsParams.newBuilder().setProductList(list).build(), (result, details) -> {
			if (result.getResponseCode() != BillingClient.BillingResponseCode.OK) {
				return;
			}
			runOnHostThread(() -> {
				for (ProductDetails product : details.getProductDetailsList()) {
					products.put(product.getProductId(), product);
					ProductDetails.OneTimePurchaseOfferDetails offer = product.getOneTimePurchaseOfferDetails();
					if (offer != null) {
						prices.put(product.getProductId(), offer.getFormattedPrice());
					}
				}
				changed();
			});
		});
	}

	// What the player owns now, from Play's cache (it works offline). A purchase still to be
	// acknowledged is acknowledged here: Google refunds one that isn't within three days.
	private void loadPurchases(int callId) {
		billing.queryPurchasesAsync(QueryPurchasesParams.newBuilder().setProductType(BillingClient.ProductType.INAPP).build(), (result, purchases) -> {
			if (result.getResponseCode() != BillingClient.BillingResponseCode.OK) {
				if (callId >= 0) {
					runOnHostThread(() -> finish(callId, "failed", result.getDebugMessage()));
				}
				return;
			}
			runOnHostThread(() -> {
				owned.clear();
				for (Purchase purchase : purchases) {
					keep(purchase);
				}
				changed();
				if (callId >= 0) {
					finish(callId, null, null);
				}
			});
		});
	}

	private void keep(Purchase purchase) {
		if (purchase.getPurchaseState() != Purchase.PurchaseState.PURCHASED) {
			return;
		}
		owned.addAll(purchase.getProducts());
		if (!purchase.isAcknowledged()) {
			billing.acknowledgePurchase(AcknowledgePurchaseParams.newBuilder().setPurchaseToken(purchase.getPurchaseToken()).build(), result -> {});
		}
	}

	@UsedByGodot
	public void purchase(int callId, String productId) {
		Activity activity = getActivity();
		ProductDetails product = products.get(productId);
		if (activity == null || billing == null || product == null) {
			finish(callId, "unavailable", "Google Play doesn't sell " + productId + " (or hasn't said so yet).");
			return;
		}
		if (purchaseCallId >= 0) {
			finish(callId, "failed", "Another purchase is already under way.");
			return;
		}
		List<BillingFlowParams.ProductDetailsParams> list = new ArrayList<>();
		list.add(BillingFlowParams.ProductDetailsParams.newBuilder().setProductDetails(product).build());
		purchaseCallId = callId;
		BillingResult result = billing.launchBillingFlow(activity, BillingFlowParams.newBuilder().setProductDetailsParamsList(list).build());
		if (result.getResponseCode() != BillingClient.BillingResponseCode.OK) {
			purchaseCallId = -1;
			finish(callId, "failed", result.getDebugMessage());
		}
	}

	// Purchases from the purchase flow, and from outside the game (a pending one completing).
	private void purchasesUpdated(@NonNull BillingResult result, @Nullable List<Purchase> purchases) {
		runOnHostThread(() -> {
			int callId = purchaseCallId;
			purchaseCallId = -1;
			int code = result.getResponseCode();
			boolean pending = false;
			if (code == BillingClient.BillingResponseCode.OK && purchases != null) {
				for (Purchase purchase : purchases) {
					pending = pending || purchase.getPurchaseState() == Purchase.PurchaseState.PENDING;
					keep(purchase);
				}
				changed();
			} else if (code == BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED) {
				loadPurchases(-1);
			}
			if (callId < 0) {
				return;
			}
			if (code == BillingClient.BillingResponseCode.OK || code == BillingClient.BillingResponseCode.ITEM_ALREADY_OWNED) {
				if (pending) {
					finish(callId, "pending", "The purchase is waiting for payment.");
				} else {
					finish(callId, null, null);
				}
			} else if (code == BillingClient.BillingResponseCode.USER_CANCELED) {
				finish(callId, "cancelled", "The player cancelled the purchase.");
			} else {
				finish(callId, "failed", result.getDebugMessage());
			}
		});
	}

	@UsedByGodot
	public void restore(int callId) {
		if (billing == null) {
			finish(callId, "unavailable", "Google Play's store isn't connected.");
			return;
		}
		loadPurchases(callId);
	}

	@UsedByGodot
	public boolean owns(String productId) {
		return owned.contains(productId);
	}

	// A Play Integrity token for Purr's online service, for this request (requestHash: the
	// service's challenge). cloudProjectNumber: the Google Cloud project linked to the game.
	@UsedByGodot
	public void integrityToken(int callId, String cloudProjectNumber, String requestHash) {
		Activity activity = getActivity();
		long project;
		try {
			project = Long.parseLong(cloudProjectNumber.trim());
		} catch (NumberFormatException e) {
			finish(callId, "unavailable", "google_play/cloud_project_number isn't set.");
			return;
		}
		if (activity == null) {
			finish(callId, "unavailable", null);
			return;
		}
		if (integrity != null) {
			requestToken(callId, requestHash);
			return;
		}
		IntegrityManagerFactory.createStandard(activity)
				.prepareIntegrityToken(StandardIntegrityManager.PrepareIntegrityTokenRequest.builder().setCloudProjectNumber(project).build())
				.addOnCompleteListener(task -> runOnHostThread(() -> {
					if (!task.isSuccessful()) {
						finish(callId, "failed", task.getException() != null ? task.getException().getMessage() : null);
						return;
					}
					integrity = task.getResult();
					requestToken(callId, requestHash);
				}));
	}

	private void requestToken(int callId, String requestHash) {
		integrity.request(StandardIntegrityManager.StandardIntegrityTokenRequest.builder().setRequestHash(requestHash).build())
				.addOnCompleteListener(task -> runOnHostThread(() -> {
					if (!task.isSuccessful()) {
						// A provider that's too old is made again next time.
						integrity = null;
						finish(callId, "failed", task.getException() != null ? task.getException().getMessage() : null);
						return;
					}
					Dictionary result = new Dictionary();
					result.put("ok", true);
					result.put("token", task.getResult().token());
					emitSignal(FINISHED.getName(), callId, result);
				}));
	}
}
