AM_CPPFLAGS = \
        $(GOA_CFLAGS)

pluginsdir = $(MISSION_CONTROL_PLUGINS_DIR)
plugins_LTLIBRARIES = \
        mcp-account-manager-goa.la

mcp_account_manager_goa_la_SOURCES = \
        mission-control-plugin.c \
        mcp-account-manager-goa.c mcp-account-manager-goa.h \
	$(NULL)

mcp_account_manager_goa_la_LIBADD = \
        $(GOA_LIBS)

mcp_account_manager_goa_la_LDFLAGS = \
        -module \
        -avoid-version
