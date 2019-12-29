cflags = -Wall -Wextra -Wno-unused-parameter -Iinclude -g \
	     `pkg-config --cflags gtk+-3.0`
ldeps  = -g `pkg-config --libs gtk+-3.0`

srcs_dir = src
objs_dir = obj
bin_dir  = bin

# I want build the client and server, separately.
# For this, linking must be unique for each program, while
# it is not necessary for compiling.
srcs   = $(addprefix $(srcs_dir)/,client.c server.c camera.c)
srcs_c = $(addprefix $(srcs_dir)/,client.c camera.c)
srcs_s = $(addprefix $(srcs_dir)/,server.c camera.c)

objs   = $(srcs:$(srcs_dir)/%.c=$(objs_dir)/%.o)
objs_c = $(srcs_c:$(srcs_dir)/%.c=$(objs_dir)/%.o)
objs_s = $(srcs_s:$(srcs_dir)/%.c=$(objs_dir)/%.o)

.PHONY: all
all: $(bin_dir)/client $(bin_dir)/server

$(bin_dir)/client: $(objs_c) | $(bin_dir)
	@gcc $(ldeps) -o $@ $^

$(bin_dir)/server: $(objs_s) | $(bin_dir)
	@gcc $(ldeps) -o $@ $^

$(objs_dir)/%.o: $(srcs_dir)/%.c | $(objs_dir)
	@gcc $(cflags) -c $< -o $@

$(objs_dir):
	@[ -d $(objs_dir) ] || mkdir $(objs_dir)

$(bin_dir):
	@[ -d $(bin_dir) ] || mkdir $(bin_dir)

.PHONY: clean
clean:
	@rm -rf $(objs_dir) $(bin_dir)
