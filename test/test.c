#include <stdio.h>

void foo(int x) {
    if (x > 0) {
        printf("In foo1\n");
    }
    else {
        printf("In foo2\n");
    }
}

void bar() {
    printf("In bar\n");
    foo(1);
    foo(-1);
}

int main() {
    printf("Program started\n");
    foo(1);
    bar();
    return 0;
}