bool normalizeEventTokenFromText(const char* raw_text, char* out_event, size_t out_capacity) {
  if (raw_text == nullptr || out_event == nullptr || out_capacity == 0U) {
    return false;
  }

  char event[kSerialLineCapacity] = {0};
  copyText(event, sizeof(event), raw_text);
  trimAsciiInPlace(event);
  if (event[0] == '\0') {
    return false;
  }

  if (startsWithIgnoreCase(event, "SC_EVENT_RAW ")) {
    char* payload = event + 13;
    trimAsciiInPlace(payload);
    if (payload[0] == '\0') {
      return false;
    }
    copyText(out_event, out_capacity, payload);
    return true;
  }

  if (startsWithIgnoreCase(event, "SC_EVENT ")) {
    char* args = event + 9;
    trimAsciiInPlace(args);
    if (args[0] == '\0') {
      return false;
    }
    char* type_text = args;
    char* name_text = nullptr;
    for (size_t index = 0U; args[index] != '\0'; ++index) {
      if (args[index] == ' ') {
        args[index] = '\0';
        name_text = &args[index + 1U];
        break;
      }
    }
    if (name_text != nullptr) {
      trimAsciiInPlace(name_text);
      if (name_text[0] == '\0') {
        name_text = nullptr;
      }
    }
    StoryEventType parsed_type = StoryEventType::kNone;
    if (!parseEventType(type_text, &parsed_type)) {
      return false;
    }
    return buildEventTokenFromTypeName(parsed_type, name_text, out_event, out_capacity);
  }

  if (startsWithIgnoreCase(event, "SERIAL ")) {
    char* name = event + 7;
    trimAsciiInPlace(name);
    toUpperAsciiInPlace(name);
    snprintf(out_event, out_capacity, "SERIAL:%s", name[0] != '\0' ? name : "BTN_NEXT");
    return true;
  }
  if (startsWithIgnoreCase(event, "BUTTON ")) {
    char* name = event + 7;
    trimAsciiInPlace(name);
    toUpperAsciiInPlace(name);
    snprintf(out_event, out_capacity, "BUTTON:%s", name[0] != '\0' ? name : "ANY");
    return true;
  }
  if (startsWithIgnoreCase(event, "ESPNOW ")) {
    char* name = event + 7;
    trimAsciiInPlace(name);
    toUpperAsciiInPlace(name);
    snprintf(out_event, out_capacity, "ESPNOW:%s", name[0] != '\0' ? name : "EVENT");
    return true;
  }
  if (startsWithIgnoreCase(event, "TIMER ")) {
    char* name = event + 6;
    trimAsciiInPlace(name);
    toUpperAsciiInPlace(name);
    snprintf(out_event, out_capacity, "TIMER:%s", name[0] != '\0' ? name : "ETAPE2_DUE");
    return true;
  }
  if (startsWithIgnoreCase(event, "ACTION ")) {
    char* name = event + 7;
    trimAsciiInPlace(name);
    toUpperAsciiInPlace(name);
    snprintf(out_event, out_capacity, "ACTION:%s", name[0] != '\0' ? name : "ACTION_FORCE_ETAPE2");
    return true;
  }

  toUpperAsciiInPlace(event);
  copyText(out_event, out_capacity, event);
  return true;
}
