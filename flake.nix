{
  description = "wayland scrim layer";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      nixpkgs,
      flake-utils,
      self,
      ...
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        nativeBuildInputs = with pkgs; [
          pkg-config
          cmake
          wayland-scanner
          wayland-protocols
          wlr-protocols
        ];
        buildInputs = with pkgs; [
          libffi # just for silent cmake warnings
          wayland
        ];
      in
      rec {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "scrim";
          version = "0.1.0";
          src =
            with (pkgs.lib.fileset);
            toSource {
              root = ./.;
              fileset = intersection (gitTracked ./.) (unions [
                ./src
                ./CMakeLists.txt
              ]);
            };
          inherit nativeBuildInputs buildInputs;
        };
        apps.default = {
          type = "app";
          program = "${packages.default}/bin/scrim";
        };
        devShells.default = pkgs.mkShell {
          inherit nativeBuildInputs buildInputs;
        };
      }
    )
    // {
      overlays.default = final: prev: {
        scrim = self.packages.${final.stdenv.hostPlatform.system}.default;
      };
      homeManagerModules.default =
        {
          config,
          lib,
          pkgs,
          ...
        }:
        let
          cfg = config.services.scrim;
        in
        {
          options.services.scrim = {
            enable = lib.mkEnableOption "wayland scrim layer";
            package = lib.mkOption {
              type = lib.types.package;
              default = self.packages.${pkgs.stdenv.hostPlatform.system}.default;
              description = "Package to use for the scrim service";
            };
          };
          config = lib.mkIf cfg.enable {
            systemd.user.services.scrim = {
              Unit = {
                Description = "wayland scrim layer";
                PartOf = [ "graphical-session.target" ];
                After = [ "graphical-session.target" ];
                Requisite = [ "graphical-session.target" ];
              };
              Service = {
                ExecStart = "${cfg.package}/bin/scrim";
                Restart = "on-failure";
                RestartSec = 3;
              };
            };
          };
        };
    };
}
