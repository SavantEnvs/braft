// Leak detection is not what this fleet fuzzes for; ASan's memory-corruption checks and UBSan are.
extern "C" int __lsan_is_turned_off() { return 1; }
