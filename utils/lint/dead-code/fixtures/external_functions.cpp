int dead_external_fixture();
int dead_external_fixture() {
    return 1;
}

int live_external_fixture();
int live_external_fixture() {
    return 2;
}

int main() {
    return live_external_fixture();
}
