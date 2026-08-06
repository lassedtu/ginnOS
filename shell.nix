{ pkgs ? import <nixpkgs> {} }:

let
  cross = pkgs.pkgsCross.i686-embedded;
in
pkgs.mkShell {
  nativeBuildInputs = [
    cross.buildPackages.gcc
    cross.buildPackages.binutils
    pkgs.nasm
    pkgs.qemu
    pkgs.python3
  ];
}
