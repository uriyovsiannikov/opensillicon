#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#define CHECK(x) do { if (!(x)) { printf("NETSURF XCB FAIL line %d\n", __LINE__); exit(1); } } while (0)

static xcb_window_t
find_browser(xcb_connection_t *connection, xcb_window_t window, unsigned depth)
{
	xcb_get_property_reply_t *name = xcb_get_property_reply(connection,
	    xcb_get_property(connection, 0, window, XCB_ATOM_WM_NAME,
	    XCB_ATOM_STRING, 0, 32), NULL);
	int found = name != NULL && xcb_get_property_value_length(name) == 7 &&
	    memcmp(xcb_get_property_value(name), "NetSurf", 7) == 0;
	free(name);
	if (found)
		return window;
	if (depth == 0)
		return XCB_WINDOW_NONE;
	xcb_query_tree_reply_t *tree = xcb_query_tree_reply(connection,
	    xcb_query_tree(connection, window), NULL);
	if (tree == NULL)
		return XCB_WINDOW_NONE;
	xcb_window_t result = XCB_WINDOW_NONE;
	xcb_window_t *children = xcb_query_tree_children(tree);
	for (int i = 0; i < xcb_query_tree_children_length(tree) && result == XCB_WINDOW_NONE; i++)
		result = find_browser(connection, children[i], depth - 1);
	free(tree);
	return result;
}

static void
key(xcb_connection_t *connection, xcb_key_symbols_t *symbols,
    xcb_window_t window, xcb_window_t root, xcb_keysym_t symbol)
{
	xcb_keycode_t *codes = xcb_key_symbols_get_keycode(symbols, symbol);
	CHECK(codes != NULL && codes[0] != XCB_NO_SYMBOL);
	xcb_key_press_event_t event = { .response_type = XCB_KEY_PRESS,
	    .detail = codes[0], .event = window, .root = root, .same_screen = 1 };
	xcb_send_event(connection, 0, window, XCB_EVENT_MASK_KEY_PRESS, (const char *)&event);
	event.response_type = XCB_KEY_RELEASE;
	xcb_send_event(connection, 0, window, XCB_EVENT_MASK_KEY_RELEASE, (const char *)&event);
	xcb_flush(connection);
	free(codes);
	usleep(40000);
}

int
main(void)
{
	xcb_connection_t *connection = NULL;
	for (unsigned i = 0; i < 300; i++) {
		connection = xcb_connect(NULL, NULL);
		if (connection != NULL && !xcb_connection_has_error(connection))
			break;
		if (connection != NULL) xcb_disconnect(connection);
		connection = NULL;
		usleep(100000);
	}
	CHECK(connection != NULL);
	xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(connection)).data->root;
	xcb_window_t window = XCB_WINDOW_NONE;
	for (unsigned i = 0; i < 300 && window == XCB_WINDOW_NONE; i++) {
		window = find_browser(connection, root, 3);
		if (window == XCB_WINDOW_NONE) usleep(100000);
	}
	CHECK(window != XCB_WINDOW_NONE);
	puts("NetSurf XCB window ready");
	sleep(5);

	/* Exercise the real server pointer as well as application event delivery. */
	xcb_warp_pointer(connection, XCB_WINDOW_NONE, window, 0, 0, 0, 0, 250, 15);
	xcb_flush(connection);
	/* Move into the URL widget, focus it with a click, then append a path. */
	xcb_motion_notify_event_t motion = { .response_type = XCB_MOTION_NOTIFY,
	    .event = window, .root = root, .same_screen = 1, .event_x = 250, .event_y = 15 };
	xcb_send_event(connection, 0, window, XCB_EVENT_MASK_POINTER_MOTION, (const char *)&motion);
	xcb_button_press_event_t button = { .response_type = XCB_BUTTON_PRESS,
	    .detail = 1, .event = window, .root = root, .same_screen = 1,
	    .event_x = 250, .event_y = 15 };
	xcb_send_event(connection, 0, window, XCB_EVENT_MASK_BUTTON_PRESS, (const char *)&button);
	button.response_type = XCB_BUTTON_RELEASE;
	xcb_send_event(connection, 0, window, XCB_EVENT_MASK_BUTTON_RELEASE, (const char *)&button);
	xcb_flush(connection);
	usleep(100000);
	xcb_key_symbols_t *symbols = xcb_key_symbols_alloc(connection);
	CHECK(symbols != NULL);
	key(connection, symbols, window, root, 0xff57); /* End */
	for (const char *p = "keyboard"; *p != 0; p++)
		key(connection, symbols, window, root, (unsigned char)*p);
	key(connection, symbols, window, root, 0xff0d); /* Return */
	puts("NetSurf XCB keyboard request submitted");
	sleep(10);

	xcb_intern_atom_reply_t *protocols = xcb_intern_atom_reply(connection,
	    xcb_intern_atom(connection, 0, 12, "WM_PROTOCOLS"), NULL);
	xcb_intern_atom_reply_t *deletion = xcb_intern_atom_reply(connection,
	    xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW"), NULL);
	CHECK(protocols != NULL && deletion != NULL);
	xcb_client_message_event_t close_event = { .response_type = XCB_CLIENT_MESSAGE,
	    .format = 32, .window = window, .type = protocols->atom };
	close_event.data.data32[0] = deletion->atom;
	xcb_send_event(connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (const char *)&close_event);
	xcb_flush(connection);
	free(protocols);
	free(deletion);
	int closed = 0;
	for (unsigned i = 0; i < 100; i++) {
		xcb_get_window_attributes_reply_t *attributes = xcb_get_window_attributes_reply(connection,
		    xcb_get_window_attributes(connection, window), NULL);
		if (attributes == NULL) { closed = 1; break; }
		free(attributes);
		usleep(100000);
	}
	CHECK(closed);
	puts("NetSurf XCB WM_DELETE passed");
	xcb_key_symbols_free(symbols);
	xcb_disconnect(connection);
	return 0;
}
