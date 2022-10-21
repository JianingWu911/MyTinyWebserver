#include <iostream>

int main() {
    char* a = const_cast<char*>("luokai");
    a[0] = '0';
    
    std::cout << a << std::endl; 
}