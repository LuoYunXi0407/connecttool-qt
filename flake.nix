{
  description = "ConnectTool Qt build with Steamworks + Qt6";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];

      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
      nixpkgsFor = forAllSystems (system: import nixpkgs { inherit system; });
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor.${system};
        in
        {
          default = pkgs.stdenv.mkDerivation rec {
            pname = "connecttool-qt";
            version = "0.1.0";

            # Keep entire working tree (including untracked) so new sources are present.
            src = ./.; 

            # Use string-based hints so evaluation succeeds even if SDK folders
            # are not present in the working tree.
            steamworksHint = "${./.}/steamworks";
            steamworksSdkHint = "${./.}/sdk";

            nativeBuildInputs = with pkgs; [
              cmake
              ninja
              pkg-config
              qt6.wrapQtAppsHook
            ];

            buildInputs = (with pkgs.qt6; [
              qtbase
              qtdeclarative
              qtsvg
              qttools
              qt5compat
            ])
            ++ (if pkgs.stdenv.isLinux then [ pkgs.qt6.qtwayland ] else [])
            ++ [ pkgs.boost ];

            cmakeFlags = [
              "-DSTEAMWORKS_PATH_HINT=${steamworksHint}"
              "-DSTEAMWORKS_SDK_DIR=${steamworksSdkHint}"
              "-DCMAKE_BUILD_TYPE=Release"
            ];

            configurePhase = "cmake -B build -S . -G Ninja $cmakeFlags";
            buildPhase = "cmake --build build";
            installPhase = "cmake --install build --prefix $out";
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor.${system};
        in
        {
          default = pkgs.mkShell {
            name = "connecttool-qt-shell";
            inputsFrom = [ self.packages.${system}.default ];
            packages = with pkgs; [ gdb clang-tools ];
            CMAKE_EXPORT_COMPILE_COMMANDS = "1";
            shellHook = ''
              echo "Qt: $(qmake --version | grep -o 'Qt version [0-9.]*' | cut -d ' ' -f 3)"
              ln -sf build/compile_commands.json .
            '';
          };
        }
      );
    };
}
