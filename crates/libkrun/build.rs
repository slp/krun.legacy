use std::{env, path::PathBuf};

fn main() {
    println!("cargo::rerun-if-changed=wrapper.h");
    println!("cargo::rustc-link-lib=krun");

    let bindings = bindgen::Builder::default()
        .header("wrapper.h")
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}
