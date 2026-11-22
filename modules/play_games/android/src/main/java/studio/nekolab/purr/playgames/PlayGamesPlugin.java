package studio.nekolab.purr.playgames;

import android.app.Activity;
import android.content.Intent;
import android.content.IntentSender;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.google.android.gms.games.AnnotatedData;
import com.google.android.gms.games.FriendsResolutionRequiredException;
import com.google.android.gms.games.LeaderboardsClient;
import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.PlayGamesSdk;
import com.google.android.gms.games.Player;
import com.google.android.gms.games.PlayerBuffer;
import com.google.android.gms.games.SnapshotsClient;
import com.google.android.gms.games.achievement.Achievement;
import com.google.android.gms.games.achievement.AchievementBuffer;
import com.google.android.gms.games.leaderboard.LeaderboardScore;
import com.google.android.gms.games.leaderboard.LeaderboardScoreBuffer;
import com.google.android.gms.games.leaderboard.LeaderboardVariant;
import com.google.android.gms.games.snapshot.Snapshot;
import com.google.android.gms.games.snapshot.SnapshotMetadata;
import com.google.android.gms.games.snapshot.SnapshotMetadataBuffer;
import com.google.android.gms.games.snapshot.SnapshotMetadataChange;
import com.google.android.gms.tasks.Task;

import org.godotengine.godot.Dictionary;
import org.godotengine.godot.Godot;
import org.godotengine.godot.plugin.GodotPlugin;
import org.godotengine.godot.plugin.SignalInfo;
import org.godotengine.godot.plugin.UsedByGodot;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Set;

/**
 * The Android side of Purr's PlayGames class: Google Play Games Services v2. Each asynchronous
 * method takes a call ID from the C++ side and answers with the "finished" signal: the ID and a
 * Dictionary with "ok", an "error" when it failed, and the method's results.
 */
public class PlayGamesPlugin extends GodotPlugin {
	private static final SignalInfo FINISHED = new SignalInfo("finished", Integer.class, Dictionary.class);
	private static final int SHOW_REQUEST = 9161;
	private static final int FRIENDS_REQUEST = 9162;
	private static final int MAX_FRIENDS = 200;

	// A friends list waiting on the player's consent to share it.
	private int friendsCallId = 0;

	private boolean authenticated = false;
	private String playerId = "";

	public PlayGamesPlugin(Godot godot) {
		super(godot);
	}

	@NonNull
	@Override
	public String getPluginName() {
		return "PlayGamesAndroid";
	}

	@NonNull
	@Override
	public Set<SignalInfo> getPluginSignals() {
		return Collections.singleton(FINISHED);
	}

	@Override
	public View onMainCreate(@Nullable Activity activity) {
		// The library also ships for Google Play's store alone, in games without Play Games (and so
		// without its app ID, which Play Games can't start without).
		if (activity != null && hasAppId(activity)) {
			PlayGamesSdk.initialize(activity);
		}
		return null;
	}

	private static boolean hasAppId(Activity activity) {
		try {
			android.os.Bundle metaData = activity.getPackageManager().getApplicationInfo(activity.getPackageName(), android.content.pm.PackageManager.GET_META_DATA).metaData;
			return metaData != null && metaData.containsKey("com.google.android.gms.games.APP_ID");
		} catch (android.content.pm.PackageManager.NameNotFoundException e) {
			return false;
		}
	}

	private void finish(int callId, Dictionary result) {
		emitSignal(FINISHED.getName(), callId, result);
	}

	private void succeed(int callId, Dictionary result) {
		result.put("ok", true);
		finish(callId, result);
	}

	private void fail(int callId, String error, @Nullable Exception exception) {
		Dictionary result = new Dictionary();
		result.put("ok", false);
		result.put("error", error);
		result.put("message", exception != null && exception.getMessage() != null ? exception.getMessage() : error);
		finish(callId, result);
	}

	// Signing in: silently if the player signed in before (Play Games signs in on its own at
	// launch), otherwise with Google's prompt when interactive.
	@UsedByGodot
	public void signIn(int callId, boolean interactive) {
		Activity activity = getActivity();
		if (activity == null) {
			fail(callId, "unavailable", null);
			return;
		}
		PlayGames.getGamesSignInClient(activity).isAuthenticated().addOnCompleteListener(check -> {
			if (check.isSuccessful() && check.getResult().isAuthenticated()) {
				loadPlayer(callId);
			} else if (interactive) {
				PlayGames.getGamesSignInClient(activity).signIn().addOnCompleteListener(signIn -> {
					if (signIn.isSuccessful() && signIn.getResult().isAuthenticated()) {
						loadPlayer(callId);
					} else {
						authenticated = false;
						fail(callId, "not_signed_in", signIn.getException());
					}
				});
			} else {
				authenticated = false;
				fail(callId, "not_signed_in", check.getException());
			}
		});
	}

	private void loadPlayer(int callId) {
		PlayGames.getPlayersClient(getActivity()).getCurrentPlayer().addOnCompleteListener(task -> {
			if (!task.isSuccessful()) {
				fail(callId, "player_unavailable", task.getException());
				return;
			}
			authenticated = true;
			playerId = task.getResult().getPlayerId();
			Dictionary result = new Dictionary();
			result.put("player_id", task.getResult().getPlayerId());
			result.put("player_name", task.getResult().getDisplayName());
			succeed(callId, result);
		});
	}

	@UsedByGodot
	public boolean isAuthenticated() {
		return authenticated;
	}

	// A one-time code Purr's online service exchanges with Google to confirm who the player is.
	@UsedByGodot
	public void requestServerAuthCode(int callId, String serverClientId) {
		PlayGames.getGamesSignInClient(getActivity()).requestServerSideAccess(serverClientId, false).addOnCompleteListener(task -> {
			if (!task.isSuccessful()) {
				fail(callId, "auth_code_unavailable", task.getException());
				return;
			}
			Dictionary result = new Dictionary();
			result.put("code", task.getResult());
			succeed(callId, result);
		});
	}

	// Achievements and leaderboards. Play Games queues these and sends them when it can.

	@UsedByGodot
	public void unlockAchievement(String id) {
		if (authenticated) {
			PlayGames.getAchievementsClient(getActivity()).unlock(id);
		}
	}

	@UsedByGodot
	public void incrementAchievement(String id, int steps) {
		if (authenticated) {
			PlayGames.getAchievementsClient(getActivity()).increment(id, steps);
		}
	}

	// Sets an incremental achievement's steps; Play Games never lowers them.
	@UsedByGodot
	public void setAchievementSteps(String id, int steps) {
		if (authenticated && steps > 0) {
			PlayGames.getAchievementsClient(getActivity()).setSteps(id, steps);
		}
	}

	// Every achievement: whether it's unlocked and, for incremental ones, its steps.
	@UsedByGodot
	public void loadAchievements(int callId) {
		PlayGames.getAchievementsClient(getActivity()).load(false).addOnCompleteListener(task -> {
			if (!task.isSuccessful()) {
				fail(callId, "achievements_unavailable", task.getException());
				return;
			}
			AchievementBuffer buffer = task.getResult().get();
			Dictionary achievements = new Dictionary();
			if (buffer != null) {
				for (Achievement achievement : buffer) {
					boolean incremental = achievement.getType() == Achievement.TYPE_INCREMENTAL;
					Dictionary entry = new Dictionary();
					entry.put("unlocked", achievement.getState() == Achievement.STATE_UNLOCKED);
					entry.put("current_steps", incremental ? achievement.getCurrentSteps() : 0);
					entry.put("total_steps", incremental ? achievement.getTotalSteps() : 0);
					achievements.put(achievement.getAchievementId(), entry);
				}
				buffer.release();
			}
			Dictionary result = new Dictionary();
			result.put("achievements", achievements);
			succeed(callId, result);
		});
	}

	// All-time scores: the best ("global"), the best among friends ("friends") or those around
	// the player's own ("around_player"); Play Games returns 25 at most.
	@UsedByGodot
	public void loadScores(int callId, String leaderboardId, String scope, int count) {
		LeaderboardsClient client = PlayGames.getLeaderboardsClient(getActivity());
		int collection = scope.equals("friends") ? LeaderboardVariant.COLLECTION_FRIENDS : LeaderboardVariant.COLLECTION_PUBLIC;
		int max = Math.max(1, Math.min(count, 25));
		Task<AnnotatedData<LeaderboardsClient.LeaderboardScores>> load = scope.equals("around_player")
				? client.loadPlayerCenteredScores(leaderboardId, LeaderboardVariant.TIME_SPAN_ALL_TIME, collection, max)
				: client.loadTopScores(leaderboardId, LeaderboardVariant.TIME_SPAN_ALL_TIME, collection, max);
		load.addOnCompleteListener(task -> {
			if (!task.isSuccessful() || task.getResult().get() == null) {
				fail(callId, "scores_unavailable", task.getException());
				return;
			}
			LeaderboardsClient.LeaderboardScores loaded = task.getResult().get();
			LeaderboardScoreBuffer buffer = loaded.getScores();
			ArrayList<Dictionary> scores = new ArrayList<>();
			for (LeaderboardScore score : buffer) {
				Dictionary entry = new Dictionary();
				entry.put("rank", score.getRank());
				entry.put("name", score.getScoreHolderDisplayName());
				entry.put("score", score.getRawScore());
				entry.put("player", score.getScoreHolder() != null && playerId.equals(score.getScoreHolder().getPlayerId()));
				scores.add(entry);
			}
			loaded.release();
			Dictionary result = new Dictionary();
			result.put("scores", scores.toArray());
			succeed(callId, result);
		});
	}

	@UsedByGodot
	public void submitScore(String leaderboardId, long score) {
		if (authenticated) {
			PlayGames.getLeaderboardsClient(getActivity()).submitScore(leaderboardId, score);
		}
	}

	@UsedByGodot
	public void showAchievements() {
		if (authenticated) {
			show(PlayGames.getAchievementsClient(getActivity()).getAchievementsIntent());
		}
	}

	@UsedByGodot
	public void showLeaderboard(String leaderboardId) {
		if (!authenticated) {
			return;
		}
		if (leaderboardId.isEmpty()) {
			show(PlayGames.getLeaderboardsClient(getActivity()).getAllLeaderboardsIntent());
		} else {
			show(PlayGames.getLeaderboardsClient(getActivity()).getLeaderboardIntent(leaderboardId));
		}
	}

	private void show(Task<Intent> intent) {
		intent.addOnSuccessListener(value -> getActivity().startActivityForResult(value, SHOW_REQUEST));
	}

	// The player's friends on Play Games: [{"id", "name"}]. The first time, Play Games asks the
	// player whether to share them with the game, and the list comes once they've answered.
	@UsedByGodot
	public void loadFriends(int callId) {
		if (!authenticated) {
			fail(callId, "not_signed_in", null);
			return;
		}
		PlayGames.getPlayersClient(getActivity()).loadFriends(MAX_FRIENDS, false).addOnCompleteListener(task -> {
			if (!task.isSuccessful()) {
				Exception exception = task.getException();
				if (exception instanceof FriendsResolutionRequiredException && friendsCallId == 0) {
					friendsCallId = callId;
					try {
						getActivity().startIntentSenderForResult(((FriendsResolutionRequiredException) exception).getResolution().getIntentSender(), FRIENDS_REQUEST, null, 0, 0, 0);
					} catch (IntentSender.SendIntentException sendException) {
						friendsCallId = 0;
						fail(callId, "friends_unavailable", sendException);
					}
					return;
				}
				fail(callId, "friends_unavailable", exception);
				return;
			}
			PlayerBuffer buffer = task.getResult().get();
			ArrayList<Dictionary> friends = new ArrayList<>();
			if (buffer != null) {
				for (Player player : buffer) {
					Dictionary entry = new Dictionary();
					entry.put("id", player.getPlayerId());
					entry.put("name", player.getDisplayName());
					friends.add(entry);
				}
				buffer.release();
			}
			Dictionary result = new Dictionary();
			result.put("friends", friends.toArray());
			succeed(callId, result);
		});
	}

	@Override
	public void onMainActivityResult(int requestCode, int resultCode, Intent data) {
		if (requestCode != FRIENDS_REQUEST || friendsCallId == 0) {
			return;
		}
		int callId = friendsCallId;
		friendsCallId = 0;
		if (resultCode == Activity.RESULT_OK) {
			loadFriends(callId);
		} else {
			fail(callId, "cancelled", null);
		}
	}

	// Saved games (snapshots), as files: a name, the bytes, a description (Purr keeps the save's
	// summary there) and when it was last modified, in milliseconds. Conflicts between devices
	// are settled by Play Games keeping the newest; Purr compares modification times itself.

	private SnapshotsClient snapshots() {
		return PlayGames.getSnapshotsClient(getActivity());
	}

	@UsedByGodot
	public void listFiles(int callId) {
		snapshots().load(true).addOnCompleteListener(task -> {
			if (!task.isSuccessful()) {
				fail(callId, "saved_games_unavailable", task.getException());
				return;
			}
			AnnotatedData<SnapshotMetadataBuffer> data = task.getResult();
			SnapshotMetadataBuffer buffer = data.get();
			ArrayList<Dictionary> files = new ArrayList<>();
			if (buffer != null) {
				for (SnapshotMetadata metadata : buffer) {
					Dictionary file = new Dictionary();
					file.put("name", metadata.getUniqueName());
					file.put("modified", metadata.getLastModifiedTimestamp());
					file.put("description", metadata.getDescription());
					files.add(file);
				}
				buffer.release();
			}
			Dictionary result = new Dictionary();
			result.put("files", files.toArray());
			succeed(callId, result);
		});
	}

	@UsedByGodot
	public void saveFile(int callId, String name, byte[] data, String description) {
		snapshots().open(name, true, SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED).addOnCompleteListener(open -> {
			if (!open.isSuccessful() || open.getResult().isConflict()) {
				fail(callId, "saved_games_unavailable", open.getException());
				return;
			}
			Snapshot snapshot = open.getResult().getData();
			snapshot.getSnapshotContents().writeBytes(data);
			SnapshotMetadataChange change = new SnapshotMetadataChange.Builder().setDescription(description).build();
			snapshots().commitAndClose(snapshot, change).addOnCompleteListener(commit -> {
				if (!commit.isSuccessful()) {
					fail(callId, "save_failed", commit.getException());
					return;
				}
				Dictionary result = new Dictionary();
				result.put("modified", commit.getResult().getLastModifiedTimestamp());
				succeed(callId, result);
			});
		});
	}

	@UsedByGodot
	public void loadFile(int callId, String name) {
		snapshots().open(name, false, SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED).addOnCompleteListener(open -> {
			if (!open.isSuccessful() || open.getResult().isConflict()) {
				fail(callId, "not_found", open.getException());
				return;
			}
			Snapshot snapshot = open.getResult().getData();
			Dictionary result = new Dictionary();
			try {
				result.put("data", snapshot.getSnapshotContents().readFully());
			} catch (java.io.IOException exception) {
				snapshots().discardAndClose(snapshot);
				fail(callId, "load_failed", exception);
				return;
			}
			result.put("modified", snapshot.getMetadata().getLastModifiedTimestamp());
			result.put("description", snapshot.getMetadata().getDescription());
			snapshots().discardAndClose(snapshot);
			succeed(callId, result);
		});
	}

	@UsedByGodot
	public void deleteFile(int callId, String name) {
		snapshots().open(name, false, SnapshotsClient.RESOLUTION_POLICY_MOST_RECENTLY_MODIFIED).addOnCompleteListener(open -> {
			if (!open.isSuccessful() || open.getResult().isConflict()) {
				// Nothing there to delete.
				succeed(callId, new Dictionary());
				return;
			}
			Snapshot snapshot = open.getResult().getData();
			SnapshotMetadata metadata = snapshot.getMetadata().freeze();
			snapshots().discardAndClose(snapshot).addOnCompleteListener(closed -> snapshots().delete(metadata).addOnCompleteListener(deleted -> {
				if (deleted.isSuccessful()) {
					succeed(callId, new Dictionary());
				} else {
					fail(callId, "delete_failed", deleted.getException());
				}
			}));
		});
	}
}
