ifneq ($(VERSION).$(PATCHLEVEL),2.4)

# Makefile frag for Linux v2.6 and v3.x

obj-$(CONFIG_XENOMAI) += arch/ nucleus/ skins/

else

# Makefile frag for Linux v2.4

subdir-$(CONFIG_XENOMAI) += arch

subdir-$(CONFIG_XENO_OPT_NUCLEUS) += nucleus

mod-subdirs := skins
subdir-y += skins

include $(TOPDIR)/Rules.make

endif
