{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs =
    inputs:
    inputs.flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [
        "x86_64-linux"
        "aarch64-darwin"
      ];
      
      perSystem =
        { system, pkgs, ... }:
        {
          devShells.default = pkgs.mkShell {
            name = "Reproducible development environment";

            packages =
              with pkgs;
              [
                git
                graphviz
                just
                gnugrep #
                findutils
                coreutils
              ];

            shellHook = ''
              export PS1=[Language]$PS1
              echo ""
              echo "Development shell loaded!"
              echo "  system:     ${system}"
              echo "  just:       $(just --version)"
              echo "  graphviz:   $(dot --version 2>&1)"
            '';
          };
        };
    };
}