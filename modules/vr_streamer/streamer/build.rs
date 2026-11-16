fn main() {
    // Embed Info.plist into the executable. macOS only grants the audio recording the game's sound
    // needs (and shows its prompt) to a process that declares why it wants it; a bare command-line
    // binary has nowhere else to say so.
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
        let plist = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("Info.plist");
        println!("cargo:rerun-if-changed={}", plist.display());
        println!("cargo:rustc-link-arg=-Wl,-sectcreate,__TEXT,__info_plist,{}", plist.display());
    }
}
