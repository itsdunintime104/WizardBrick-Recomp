# LEGO Harry Potter: Years 1-4. Rules: engine windows/build/game.mk; names: ../game.conf.
ENGINE ?= $(abspath ../engine)
GAME_SRCS := $(wildcard $(abspath ../runtime/src)/*.c)
# lhp_win32.exe: entry RVA, seven sections.
GAME_CFLAGS := -I $(abspath ../runtime/include) -DPE_ENTRY_RVA=0x00669393u -DPE_SECTION_COUNT=7u \
	-DGAME_SCREEN_WIDTH_VA=0x00ED889Cu -DGAME_SCREEN_HEIGHT_VA=0x00ED88A0u
GAME_TESTS := $(abspath ../tests/game_tests.sh)
include $(ENGINE)/windows/build/game.mk
