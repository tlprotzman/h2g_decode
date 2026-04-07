Decoding for HGCROC data, designed for ePIC LFHCal and EEEMCal test beams.

Instructions for installing
1. Clone the repository `git clone git@github.com:tlprotzman/h2g_decode.git`
2. Change to the directory `cd h2g_decode`
3. make build directory `mkdir build`
4. make install directory `mkdir install` (This is where h2g_decode package will go)
5. Configure cmake `cmake -S . -B build -DCMAKE_INSTALL_PREFIX=install`
6. Build and install `cmake --build build --target install`

Instructions for uninstalling
1. Remove all items in the build folder above `rm -r build/*`
2. Remove all items in the install folder above `rm -r install/*`

This creates an executable to run in a standalone fashion as well as a shared library to link against and installs files necessary for other cmake projects to find this one as a package using cmake's "find_package" function. Package name will H2G_DECODE
