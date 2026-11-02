# ЛР 8. Час, локалізація і служби. Ціль за замовчуванням збирає всі чотири програми.
CC      ?= gcc
CFLAGS  ?= -std=c17 -O2 -g -Wall -Wextra
PROGS    = clocks ticker whenis loc

# Частина 5: куди встановлювати програми і юніти служб користувача
LIBDIR   = $(HOME)/.local/lib/lab08
UNITDIR  = $(HOME)/.config/systemd/user

.PHONY: all clean units uninstall-units
all: $(PROGS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@

units: ticker whenis
	install -d $(LIBDIR) $(UNITDIR)
	install -m 755 ticker whenis $(LIBDIR)/
	install -m 644 units/lab08-ticker.service units/lab08-stamp.service units/lab08-stamp.timer $(UNITDIR)/
	systemctl --user daemon-reload

uninstall-units:
	-systemctl --user disable --now lab08-ticker.service lab08-stamp.timer
	rm -f $(UNITDIR)/lab08-ticker.service $(UNITDIR)/lab08-stamp.service $(UNITDIR)/lab08-stamp.timer
	rm -rf $(LIBDIR)
	-systemctl --user daemon-reload

clean:
	rm -f $(PROGS) *.o
