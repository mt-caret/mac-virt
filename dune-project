(lang dune 3.22)

(name mac-virt)

(generate_opam_files true)

(source
 (github mtakeda/mac-virt))

(authors "M. Takeda <mtakeda@oeconomia.jp>")

(maintainers "M. Takeda <mtakeda@oeconomia.jp>")

(license MIT)

(package
 (name mac-virt)
 (synopsis "OCaml bindings to the Apple Virtualization framework")
 (description
  "OCaml bindings to Apple's Virtualization.framework for creating and running virtual machines on Apple silicon, in the Jane Street style.")
 (depends
  (ocaml
   (>= 5.2))
  core
  core_unix
  ppx_jane))
