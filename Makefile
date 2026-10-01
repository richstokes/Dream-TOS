TARGET = build/dream-tos.elf
KOS_BUILD_SUBARCHS = pristine
all: $(TARGET)
include $(KOS_BASE)/Makefile.rules
U = upstream/emutos
GEN = build/generated
AES = $(wildcard $(U)/aes/*.c)
DESK = $(wildcard $(U)/desk/*.c)
VDI = $(filter-out $(U)/vdi/vdi_textblit.c,$(wildcard $(U)/vdi/*.c))
UTIL = $(addprefix $(U)/util/,gemdos.c intmath.c miscutil.c optimize.c rectfunc.c)
BIOS = $(addprefix $(U)/bios/,font.c lineainit.c vt52.c conout.c fnt_st_6x6.c fnt_st_8x8.c fnt_st_8x16.c fnt_off_6x6.c fnt_off_8x8.c)
CLI = $(wildcard $(U)/cli/*.c)
CORE = $(AES) $(DESK) $(VDI) $(UTIL) $(BIOS) $(CLI) $(wildcard $(U)/bdos/fs*.c) $(U)/bdos/time.c $(wildcard src/dreamcast/*.c)
GENSRC = $(addprefix $(GEN)/,desk_rsc.c gem_rsc.c icons.c mforms.c)
OBJS = $(patsubst %.c,build/obj/%.o,$(CORE) $(GENSRC)) $(patsubst %.S,build/obj/%.o,$(wildcard src/dreamcast/*.S))
INC = -Iinclude -I$(GEN) $(addprefix -I$(U)/,include aes desk vdi bios bdos)
PORTFLAGS = -DMACHINE_DREAMCAST -Dtimer_init=etos_timer_init -Dtimer_exit=etos_timer_exit -DWITH_AES=1 -DWITH_CLI=1 -DEMU_VERSION=\"1.4.0-DC\" -fpack-struct=2 -fno-strict-aliasing -std=gnu99 -Wno-error -Wno-unused-parameter
build/obj/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(KOS_CC) $(KOS_CFLAGS) -O2 $(INC) $(PORTFLAGS) -MMD -MP -c $< -o $@
build/obj/%.o: %.S
	@mkdir -p $(dir $@)
	$(KOS_CC) $(KOS_CFLAGS) -c $< -o $@
# KOS headers and structs use the SDK's normal alignment, unlike legacy GEM.
build/obj/src/dreamcast/hal.o build/obj/src/dreamcast/hal_vmu.o build/obj/src/dreamcast/hal_settings.o build/obj/src/dreamcast/hal_net.o build/obj/src/dreamcast/hal_sd.o build/obj/src/dreamcast/hal_audio.o: build/obj/src/dreamcast/%.o: src/dreamcast/%.c
	@mkdir -p $(dir $@)
	$(KOS_CC) $(KOS_CFLAGS) -O2 -Iinclude -MMD -MP -c $< -o $@
$(TARGET): $(OBJS)
	kos-cc -o $@ $(OBJS) -Wl,-Map,build/dream-tos.map
clean:
	rm -rf build/obj build/generated build/dream-tos.elf build/dream-tos.map
-include $(OBJS:.o=.d)
.PHONY: all clean
