"use strict";

class CanvasImage {
	static init() {
		/*var canvas = Browser.document.createElement("canvas");
		if (canvas != null) {
			context = canvas.getContext("2d");
			canvas.width = 2048;
			canvas.height = 2048;
			context.globalCompositeOperation = "copy";
		}*/
	}
	
	constructor(width, height, format, renderTarget) {
		this.myWidth = width;
        this.myHeight = height;
		this.format = format;
		this.renderTarget = renderTarget;
		this.image = null;
		this.video = null;
		if (renderTarget) createTexture();
	}
	
	get g1() {
		//if (graphics1 == null) {
		//	graphics1 = new kha.graphics2.Graphics1(this);
		//}
		//return graphics1;
        return null;
	}
	
	get g2() {
		/*if (g2canvas == null) {
			var canvas: Dynamic = Browser.document.createElement("canvas");
			image = canvas;
			var context = canvas.getContext("2d");
			canvas.width = width;
			canvas.height = height;
			g2canvas = new CanvasGraphics(context, width, height);
		}
		return g2canvas;*/
        return null;
	}
	
	get g4() {
		return null;
	}
	
	get width() {
		return this.myWidth;
	}

	get height() {
		return this.myHeight;
	}

	get realWidth() {
		return this.myWidth;
	}

	get realHeight() {
		return this.myHeight;
	}
	
	isOpaque(x, y) {
		if (this.data === null) {
			if (this.context === null) return true;
			else createImageData();
		}
		return (this.data.data[y * (image.width | 0) * 4 + x * 4 + 3] != 0);
	}
	
	at(x, y) {
		if (this.data == null) {
			if (this.context == null) return Color.Black;
			else createImageData();
		}
		return Color.fromValue(data.data[y * Std.int(image.width) * 4 + x * 4 + 0]);
	}
	
	createImageData() {
		context.strokeStyle = "rgba(0,0,0,0)";
		context.fillStyle = "rgba(0,0,0,0)";
		context.fillRect(0, 0, image.width, image.height);
		context.drawImage(image, 0, 0, image.width, image.height, 0, 0, image.width, image.height);
		data = context.getImageData(0, 0, image.width, image.height);
	}
	
	static upperPowerOfTwo(v) {
		v--;
		v |= v >>> 1;
		v |= v >>> 2;
		v |= v >>> 4;
		v |= v >>> 8;
		v |= v >>> 16;
		v++;
		return v;
	}
	
    lock(level) {
		bytes = Bytes.alloc(format == TextureFormat.RGBA32 ? 4 * width * height : width * height);
		return bytes;
	}
	
	unlock() {
		
	}
	
	unload() {
		
	}
}
