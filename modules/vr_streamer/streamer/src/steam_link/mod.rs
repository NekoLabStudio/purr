//! A Steam Link host: the Steam Link app on a Meta Quest (or Valve's own headset) connects to the
//! game on this Mac as it would to SteamVR on a PC. Steam on a Mac can't do VR, so this announces
//! the Mac as a host of its own beside Steam's, "<computer> (VR)", pairs headsets with a PIN, and
//! streams over Valve's VR protocol (SVL).
//!
//! What the protocol is, Purr knows from OpenVRLink's documentation
//! (github.com/shinyquagsire23/OpenVRLink, docs/), written from Valve's headset app; host
//! behaviour there is what was seen on the wire from Steam. This speaks version 14, the one those
//! captures show.

pub mod broadcast;
pub mod crypto;
pub mod devices;
pub mod fec;
pub mod host;
pub mod link;
pub mod pacing;
pub mod protobuf;
pub mod rate;
pub mod session;
pub mod values;
pub mod video;
