#ifndef WEBCONTROLLER_H
#define WEBCONTROLLER_H
//#include "WebView.h"
#include <gtk/gtkwidget.h>

struct WebChatController {
	GtkWidget* view;
	/* TODO: separate the DOM */
} ;

//this typedef is to keep the core more ui-generic
//if you want to change the "chat part", just avoid including Web*.h,
//include your own files that define similar things, write methods
//with the same prototype and you're done
typedef struct WebChatController UIChat;

//ricorda:i metodi pubblici sono di tipo uichat_*, non webchat_*
UIChat uichat_new();
void uichat_add_msg(char* from, char* msg);
void uichat_destroy(UIChat*);
//TODO: enum per il tipo dei messaggi

#endif

