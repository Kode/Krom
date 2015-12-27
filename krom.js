"use strict";

require(['domReady', 'Color', 'Font', 'CanvasGraphics'], function (domReady, Color, Font, CanvasGraphics) {
	domReady(function () {
		let canvas = document.getElementById('krom');
		let g = new CanvasGraphics(canvas.getContext('2d'), 640, 480);

		let hot = null;
		let active = null;
		let lastX = -1;
		let lastY = -1;

		function setMouseXY(event) {
			var rect = canvas.getBoundingClientRect();
			var borderWidth = canvas.clientLeft;
			var borderHeight = canvas.clientTop;
			lastX = (event.clientX - rect.left - borderWidth) * canvas.width / (rect.width - 2 * borderWidth);
			lastY = (event.clientY - rect.top - borderHeight) * canvas.height / (rect.height - 2 * borderHeight);
		}

		canvas.onmousedown = function (event) {
			
		};

		canvas.onmousemove = function (event) {
			setMouseXY(event);
		};

		function hash(text) {
			let num = 0;
			for (let i = 0; i < text.length; ++i) {
				num += text.charCodeAt(i);
			}
			return num;
		}

		function button(text, x, y, w, h) {
			let id = hash(text);
			
			if (lastX >= x && lastX <= x + w && lastY >= y && lastY <= y + h) {
				hot = id;
			}
			else {
				hot = null;
			}
			
			g.color = Color.Black;
			g.fillRect(x + 10, y + 10, w, h);
			if (id === hot) g.color = Color.fromFloats(0.2, 0.2, 1.0);
			else g.color = Color.Blue;
			g.fillRect(x, y, w, h);
			g.color = Color.Black;
			g.drawString(text, x + 20, y + 20);
		}

		function render() {
			g.begin();
			let font = new Font('Arial', null, 24);
			g.font = font;
			button('bla', 10, 10, 200, 100);
			g.end();
		}

		function animate(timestamp) {
			requestAnimationFrame(animate);
			render();
		}

		requestAnimationFrame(animate);

	});
});
