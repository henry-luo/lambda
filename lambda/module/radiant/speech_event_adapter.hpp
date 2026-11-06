#pragma once

#include <stdint.h>
struct DomElement;
struct SpeechEventFields;
bool lambda_radiant_speech_event(DomElement* target, uint64_t token, const char* type,
                                  const SpeechEventFields* fields);
