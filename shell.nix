{ pkgs ? import <nixpkgs> {} }:

let
  cross = pkgs.pkgsCross.i686-embedded;
  cross64 = pkgs.pkgsCross.x86_64-embedded;
in
pkgs.mkShell {
  nativeBuildInputs = [
    cross.buildPackages.gcc
    cross.buildPackages.binutils
    cross64.buildPackages.gcc
    cross64.buildPackages.binutils
    pkgs.nasm
    pkgs.qemu
    pkgs.python3
    pkgs.gdb
    pkgs.mtools
    pkgs.e2fsprogs
    pkgs.git
  ];

  # UEFI firmware for make run-uefi
  shellHook = ''
    export OVMF_CODE=${pkgs.OVMF.fd}/FV/OVMF_CODE.fd
  '';
}
