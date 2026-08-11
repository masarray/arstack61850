"use strict";

// app.js predates the smart host profile compiler and installs a lightweight
// browser XML listener on the engineering-file input. Replace that input node
// before the profile bridge binds so vendor-dialect handling has exactly one
// semantic authority: the C++ SCL engine. This avoids a stricter/shallower DOM
// pre-check disagreeing with the tolerant normalized parser.
if (els.sclFile) {
  const freshInput = els.sclFile.cloneNode(true);
  els.sclFile.replaceWith(freshInput);
  els.sclFile = freshInput;
}
