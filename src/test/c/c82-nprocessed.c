#define MACRO1(x) (x)
#define MACRO2(y) (y+1)

int function1(void) {
    return MACRO1(1);
}

int function2(void) {
    return MACRO2(function1());
}
