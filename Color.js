"use strict";

let invMaxChannelValue = 1 / 255;

class Color {	
	static fromValue(value) {
		return new Color(value);
	}
	
	static fromBytes(r, g, b, a = 255) {
		return new Color((a << 24) | (r << 16) | (g << 8) | b);
	}
	
	static fromFloats(r, g, b, a = 1) {
		return new Color((((a * 255) | 0) << 24) | (((r * 255) | 0) << 16) | (((g * 255) | 0) << 8) | ((b * 255) | 0));
	}
	
	static fromString(value) {
		if ((value.length == 7 || value.length == 9) && value.charCodeAt(0) === "#".charCodeAt(0)) {
			var colorValue = parseInt("0x" + value.substr(1));
			if (value.length == 7) {
				colorValue += 0xFF000000;
			}
			return Color.fromValue(colorValue);
		}
		else {
			throw "Invalid Color string: '" + value + "'";
		}
	}
	
	constructor(value) {
		this.value = value;
	}
	
	get Rb() {
		return (this.value & 0x00ff0000) >>> 16;
	}
	
	get Gb() {
		return (this.value & 0x0000ff00) >>> 8;
	}
	
	get Bb() {
		return this.value & 0x000000ff;
	}
	
	get Ab() {
		return this.value >>> 24;
	}

	set Rb(i) {
		this.value = (this.Ab << 24) | (i << 16) | (this.Gb << 8) | this.Bb;
	}
	
	set Gb(i) {
		this.value = (this.Ab << 24) | (this.Rb << 16) | (i << 8) | this.Bb;
	}
	
	set Bb(i) {
		this.value = (this.Ab << 24) | (this.Rb << 16) | (this.Gb << 8) | i;
	}
	
	set Ab(i) {
		this.value = (i << 24) | (this.Rb << 16) | (this.Gb << 8) | this.Bb;
	}

	get R() {
		return this.Rb * invMaxChannelValue;
	}
	
	get G() {
		return this.Gb * invMaxChannelValue;
	}
	
	get B() {
		return this.Bb * invMaxChannelValue;
	}
	
	get A() {
		return this.Ab * invMaxChannelValue;
	}

	set R(f) {
		this.value = (((this.A * 255) | 0) << 24) | (((f * 255) | 0) << 16) | (((this.G * 255) | 0) << 8) | ((this.B * 255) | 0);
	}

	set G(f) {
		this.value = (((this.A * 255) | 0) << 24) | (((this.R * 255) | 0) << 16) | (((f * 255) | 0) << 8) | ((this.B * 255) | 0);
	}

	set B(f) {
		this.value = (((this.A * 255) | 0) << 24) | (((this.R * 255) | 0) << 16) | (((this.G * 255) | 0) << 8) | ((f * 255) | 0);
	}

	set A(f) {
		this.value = (((f * 255) | 0) << 24) | (((this.R * 255) | 0) << 16) | (((this.G * 255) | 0) << 8) | ((this.B * 255) | 0);
	}
}

Color.Black = Color.fromValue(0xff000000);
Color.White = Color.fromValue(0xffffffff);
Color.Red = Color.fromValue(0xffff0000);
Color.Blue = Color.fromValue(0xff0000ff);
Color.Green = Color.fromValue(0xff00ff00);
Color.Magenta = Color.fromValue(0xffff00ff);
Color.Yellow = Color.fromValue(0xffffff00);
Color.Cyan = Color.fromValue(0xff00ffff);
Color.Purple = Color.fromValue(0xff800080);
Color.Pink = Color.fromValue(0xffffc0cb);
Color.Orange = Color.fromValue(0xffffa500);
