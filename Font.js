"use strict";

define([], function () {
	class Font {
		constructor(name, style, size) {
			this.name = name;
			this.style = style;
			this.size = size;
		}
		
		getHeight() {
			return this.size;
		}

		charWidth(ch) {
			return this.stringWidth(ch);
		}

		charsWidth(ch, offset, length) {
			return this.stringWidth(ch.substr(offset, length));
		}

		stringWidth(str) {
			return Painter.stringWidth(this, str);
		}

		getBaselinePosition() {
			return 0;
		}
	}
	
	return Font;
});
