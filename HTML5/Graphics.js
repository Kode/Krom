"use strict";

define(['Color', 'FastMatrix3'], function (Color, FastMatrix3) {
	class Graphics {
		begin(clear /*= true*/, clearColor /*= null*/) { }
		end() { }
		flush() { }
		
		//scale-filtering
		//draw/fillPolygon
		
		clear(color /*= null*/) { }
		
		drawImage(img, x, y) {
			this.drawSubImage(img, x, y, 0, 0, img.width, img.height);
		}
		
		drawSubImage(img, x, y, sx, sy, sw, sh) {
			this.drawScaledSubImage(img, sx, sy, sw, sh, x, y, sw, sh);
		}

		drawScaledImage(img, dx, dy, dw, dh) {
			this.drawScaledSubImage(img, 0, 0, img.width, img.height, dx, dy, dw, dh);
		}

		drawScaledSubImage(image, sx, sy, sw, sh, dx, dy, dw, dh) { }
		
		drawRect(x, y, width, height, strength /*= 1.0*/) { }
		fillRect(x, y, width, height) { }
		drawString(text, x, y) { }
		drawLine(x1, y1, x2, y2, strength /*= 1.0*/) { }
		drawVideo(video, x, y, width, height) { }
		fillTriangle(x1, y1, x2, y2, x3, y3) { }
		
		get imageScaleQuality() {
			return ImageScaleQuality.Low;
		}
		
		set imageScaleQuality(value) { }
		
		/**
		The color value is used for geometric primitives as well as for images. Remember to set it back to white to draw images unaltered.
		*/
		get color() {
			return Color.Black;
		}
		
		set color(color) { }
			
		get font() {
			return null;
		}
		
		set font(font) { }
		
		get fontSize() {
			return this.myFontSize;
		}
		
		set fontSize(value) {
			this.myFontSize = value;
		}
		
		pushTransformation(transformation) {
			this.setTransformation(transformation);
			this.transformations.push(transformation);
		}
		
		popTransformation() {
			let ret = this.transformations.pop();
			this.setTransformation(this.transformation);
			return ret;
		}
		
		// works on the top of the transformation stack
		get transformation() {
			return this.transformations[this.transformations.length - 1];
		}
		
		set transformation(transformation) {
			this.setTransformation(transformation);
			this.transformations[this.transformations.length - 1] = this.transformation;
		}
		
		translation(tx, ty) {
			return FastMatrix3.translation(tx, ty).multmat(this.transformation);
		}
		
		translate(tx, ty) {
			this.transformation = this.translation(tx, ty);
		}
		
		pushTranslation(tx, ty) {
			this.pushTransformation(this.translation(tx, ty));
		}
		
		rotation(angle, centerx, centery) {
			return FastMatrix3.translation(centerx, centery).multmat(FastMatrix3.rotation(angle)).multmat(FastMatrix3.translation(-centerx, -centery)).multmat(this.transformation);
		}
		
		rotate(angle, centerx, centery) {
			this.transformation = this.rotation(angle, centerx, centery);
		}
		
		pushRotation(angle, centerx, centery) {
			this.pushTransformation(this.rotation(angle, centerx, centery));
		}
			
		pushOpacity(opacity) {
			this.setOpacity(opacity);
			this.opacities.push(opacity);
		}
		
		popOpacity() {
			var ret = this.opacities.pop();
			this.setOpacity(this.opacity);
			return ret;
		}
		
		// works on the top of the opacity stack
		get opacity() {
			return this.opacities[this.opacities.length - 1];
		}
		
		set opacity(opacity) {
			this.setOpacity(opacity);
			this.opacities[this.opacities.length - 1] = opacity;
		}
		
		scissor(x, y, width, height) { }
		
		disableScissor() { }
		
		setBlendingMode(source, destination) { }
		
		constructor() {
			this.transformations = [];
			this.transformations.push(FastMatrix3.identity());
			this.opacities = [];
			this.opacities.push(1);
			this.myFontSize = 12;
		}
		
		setTransformation(transformation) { }
		
		setOpacity(opacity) { }
	}
	
	return Graphics;
});