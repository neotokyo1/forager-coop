// Plain strings shared with the native co-op on the game thread.
#define Main
  global.fc_chat_printing = false;
  variable_global_set("fc_chat_out_v1", "");
  variable_global_set("fc_chat_in_v1", "");

#define OnChatMessage(_message)
  if(global.fc_chat_printing || !is_string(_message)) exit;
  if(string_length(_message) == 0 || string_length(_message) > 240) exit;
  if(string_pos(chr(10), _message) || string_pos(chr(13), _message)) exit;
  if(string_char_at(_message, 1) == "/") exit;
  var _queued = variable_global_get("fc_chat_out_v1");
  if(string_length(_queued) < 2048) {
    if(_queued != "") _queued += chr(10);
    variable_global_set("fc_chat_out_v1", _queued + _message);
  }

#define OnStep
  var _line = variable_global_get("fc_chat_in_v1");
  if(is_string(_line) && _line != "") {
    variable_global_set("fc_chat_in_v1", "");
    global.fc_chat_printing = true;
    Trace(_line);
    global.fc_chat_printing = false;
  }
