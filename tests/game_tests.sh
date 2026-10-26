# HP's tests for engine windows/tests/run_all.sh --tests (bash, sourced).
def_hp_patches() {
    FLAGS=("${COMMON[@]}" "$TESTS_DIR/test_hp_patches.c" "$TESTS_DIR/../runtime/src/hp_patches.c")
    LIBS=(); ENVS=()
}
C_TESTS="$C_TESTS hp_patches"
def_hp_analyze_pe() {
    PYTEST=$TESTS_DIR/test_hp_analyze_pe.py
}
def_scene_list() {
    PYTEST=$TESTS_DIR/test_scene_list.py
}
PY_TESTS="$PY_TESTS hp_analyze_pe scene_list"
