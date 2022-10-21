#include <exception>
#include <iostream>
using namespace std;

int main() {
    cout << "start" << endl;
    if (true) {
        cout << "main error" << endl;
        throw exception();
    }
    cout << "end..." << endl;
}

