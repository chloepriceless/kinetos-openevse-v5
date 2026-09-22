// HTTP control endpoint for the Kinetos additions (used by the Kinetos page of the GUI).
#include <Arduino.h>
#include "kinetos_web.h"
#include "kinetos_p14a.h"
#include "web_server.h"

// POST /kinetos/p14a with body "1"/"0" (or ?active=1): remote §14a dimming, like the MQTT topic
static void handleP14a(MongooseHttpServerRequest *request)
{
  MongooseHttpServerResponseStream *response;
  if(false == requestPreProcess(request, response)) {
    return;
  }
  String body = request->hasParam("active") ? request->getParam("active") : request->body().toString();
  body.trim();
  bool active = body == "1" || body.equalsIgnoreCase("true") || body.equalsIgnoreCase("on");
  kinetosP14a.setRemote(active);
  response->setCode(config_kinetos_p14a_enabled() ? 200 : 409);
  response->printf("{\"p14a_remote\":%s,\"enabled\":%s}", active ? "true" : "false",
                   config_kinetos_p14a_enabled() ? "true" : "false");
  request->send(response);
}

void kinetos_web_register(MongooseHttpServer &server)
{
  server.on("/kinetos/p14a$", handleP14a);
}
