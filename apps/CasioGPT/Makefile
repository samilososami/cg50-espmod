.SUFFIXES:

ifeq ($(strip $(FXCGSDK)),)
$(error FXCGSDK no esta definido. Usa ../tools/build-g3a)
endif

include $(FXCGSDK)/toolchain/prizm_rules

TARGET      := CASIOGPT
BUILD       := build
SOURCES     := src
DATA        :=
INCLUDES    := ../../firmware/casioesp_wifi

MKG3AFLAGS  := -n basic:CasioGPT -i uns:../unselected.bmp -i sel:../selected.bmp

CFLAGS      := -Os -Wall $(MACHDEP) $(INCLUDE) -ffunction-sections -fdata-sections
CXXFLAGS    := $(CFLAGS) -fno-exceptions
LDFLAGS     := $(MACHDEP) -T$(FXCGSDK)/toolchain/prizm.x -Wl,-static -Wl,-gc-sections
GCC_DIVLIB  := $(shell $(CC) $(MACHDEP) -print-file-name=libgcc-Os-4-200.a)
GCC_ICACHE  := $(shell $(CC) $(MACHDEP) -print-file-name=libic_invalidate_array_4a.a)
LIBS        := -Wl,--start-group -lc -lfxcg $(GCC_ICACHE) $(GCC_DIVLIB) -lgcc -Wl,--end-group
LIBDIRS     :=

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif
export OFILES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(sFILES:.s=.o) $(SFILES:.S=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD) -I$(LIBFXCG_INC)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(LIBFXCG_LIB)
.PHONY: all clean
all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
$(BUILD):
	@mkdir -p $@
clean:
	@rm -rf $(BUILD)
	@rm -f $(OUTPUT).bin $(OUTPUT).g3a
else
DEPENDS := $(OFILES:.o=.d)
$(OUTPUT).g3a: $(OUTPUT).bin
$(OUTPUT).bin: $(OFILES)
-include $(DEPENDS)
endif
