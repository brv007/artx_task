
ifeq ($(src_dir),)
src_dir := $(CURDIR)
endif

base_dir    := ../
target_name := test_task_1
src_files   := $(wildcard $(src_dir)/*.c)

CC = gcc
COPT :=
DEBUGFLAGS := -g -O0 -D_DEBUG
INCLUDE :=
LD_LIBS := -lev

wrk_dir  := $(base_dir)/obj/$(target_name)
bin_dir  := $(src_dir)/$(base_dir)bin/
dirs     := $(wrk_dir) $(bin_dir)
target   := $(bin_dir)/$(target_name)
depends  :=
objs     := $(patsubst %.c,%.o,$(src_files))
makefile := $(src_dir)/Makefile

.PHONY: target

target: $(dirs)
	@make --directory=$(wrk_dir) --makefile=$(makefile) $(target) src_dir=$(src_dir)



VPATH := $(src_dir)
$(target): $(notdir $(objs)) $(depends) $(makefile)
	$(CC)  -o $@ $(notdir $(objs)) $(LD_LIBS)

#
clean: $(depends)
	@rm -rf $(wrk_dir)
	@rm -rf $(bin_dir)/$(target_name)

%.o: %.c $(makefile)
	$(CC) $(COPT) $(CFLAGS) $(INCLUDE) $(DEBUGFLAGS) -c -MD $<

ifneq ($(wildcard *.d),)
include $(wildcard *.d)
endif


$(dirs):
	@mkdir -p $@
