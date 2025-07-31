#include <stdio.h>
//#include "dummy_main.h"
// Function to return the nth Fibonacci number
int fibonacci(int n) {
    if (n <= 0)
        return 0;
    else if (n == 1)
        return 1;
    else {
        int a = 0, b = 1, fib;
        for (int i = 2; i <= n; i++) {
            fib = a + b;
            a = b;
            b = fib;
        }
        return b;
    }
}

int main() {
    int n=30;
    int result = fibonacci(30);
    printf("The %dth Fibonacci number is: %d\n", n, result);
    fflush(stdout);
    return 0;
}