#ifndef KINETOS_WEB_H
#define KINETOS_WEB_H

#include <MongooseHttpServer.h>

// Registers /kinetos/p14a (remote §14a control)
void kinetos_web_register(MongooseHttpServer &server);

#endif // KINETOS_WEB_H
