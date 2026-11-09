// Boost.JSON can either be linked as a library or as a compiled library.
// instead exactly one cpp file in the project contains
// the header which compiles the library into the program, this avoids multiple definition linker errors.
#include <boost/json/src.hpp> // <-- doesn't need to be included anywhere else