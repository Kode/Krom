"use strict";

let g = new CanvasGraphics(document.getElementById('krom').getContext('2d'), 640, 480);

function button(text) {
	g.color = Color.Black;
	g.fillRect(20, 20, 200, 100);
	g.color = Color.Blue;
	g.fillRect(10, 10, 200, 100);
}

function render() {
	g.begin();
    button('bla');
	g.end();
}

function animate(timestamp) {
	requestAnimationFrame(animate);
	render();
}

requestAnimationFrame(animate);
