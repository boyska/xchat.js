#ifndef WEBCONTROLLER_H
#define WEBCONTROLLER_H
//#include "WebView.h"
#include <gtk/gtkwidget.h>

typedef struct WebChatController {
	GtkWidget* view;
	/* TODO: separate the DOM */
} WebChatController;

//this typedef is to keep the core more ui-generic
//if you want to change the "chat part", just avoid including Web*.h,
//include your own files that define similar things, write methods
//with the same prototype and you're done
typedef struct WebChatController UIChat;

//ricorda:i metodi pubblici sono di tipo uichat_*, non webchat_*
UIChat *uichat_new(char *from, int type);
void uichat_add_msg(UIChat *chat, char* from, char* msg);
void uichat_destroy(UIChat*);
//TODO: enum per il tipo dei messaggi

#endif

