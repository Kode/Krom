"use strict";

let width = 3;
let height = 3;

class FastMatrix3 {
	constructor(_00, _10, _20,
				_01, _11, _21,
				_02, _12, _22) {
		this._00 = _00; this._10 = _10; this._20 = _20;
		this._01 = _01; this._11 = _11; this._21 = _21;
		this._02 = _02; this._12 = _12; this._22 = _22;
	}

	static translation(x, y) {
		return new FastMatrix3(
			1, 0, x,
			0, 1, y,
			0, 0, 1
		);
	}

	static empty() {
		return new FastMatrix3(
			0, 0, 0,
			0, 0, 0,
			0, 0, 0
		);
	}

	static identity() {
		return new FastMatrix3(
			1, 0, 0,
			0, 1, 0,
			0, 0, 1
		);
	}

	static scale(x, y) {
		return new FastMatrix3(
			x, 0, 0,
			0, y, 0,
			0, 0, 1
		);
	}

	static rotation(alpha) {
		return new FastMatrix3(
			Math.cos(alpha), -Math.sin(alpha), 0,
			Math.sin(alpha), Math.cos(alpha), 0,
			0, 0, 1
		);
	}

	add(m) {
		return new FastMatrix3(
			this._00 + m._00, this._10 + m._10, this._20 + m._20,
			this._01 + m._01, this._11 + m._11, this._21 + m._21,
			this._02 + m._02, this._12 + m._12, this._22 + m._22
		);
	}

	sub(m) {
		return new FastMatrix3(
			this._00 - m._00, this._10 - m._10, this._20 - m._20,
			this._01 - m._01, this._11 - m._11, this._21 - m._21,
			this._02 - m._02, this._12 - m._12, this._22 - m._22
		);
	}

	mult(value) {
		return new FastMatrix3(
			this._00 * value, this._10 * value, this._20 * value,
			this._01 * value, this._11 * value, this._21 * value,
			this._02 * value, this._12 * value, this._22 * value
		);
	}

	transpose() {
		return new FastMatrix3(
			this._00, this._01, this._02,
			this._10, this._11, this._12,
			this._20, this._21, this._22
		);
	}

	trace() {
		return this._00 + this._11 + this._22;
	}

	multmat(m) {
		return new FastMatrix3(
			this._00 * m._00 + this._10 * m._01 + this._20 * m._02, this._00 * m._10 + this._10 * m._11 + this._20 * m._12, this._00 * m._20 + this._10 * m._21 + this._20 * m._22,
			this._01 * m._00 + this._11 * m._01 + this._21 * m._02, this._01 * m._10 + this._11 * m._11 + this._21 * m._12, this._01 * m._20 + this._11 * m._21 + this._21 * m._22,
			this._02 * m._00 + this._12 * m._01 + this._22 * m._02, this._02 * m._10 + this._12 * m._11 + this._22 * m._12, this._02 * m._20 + this._12 * m._21 + this._22 * m._22
		);
	}

	multvec(value) {
		var w = this._02 * value.x + this._12 * value.y + this._22 * 1;
		var x = (this._00 * value.x + this._10 * value.y + this._20 * 1) / w;
		var y = (this._01 * value.x + this._11 * value.y + this._21 * 1) / w;
		return new FastVector2(x, y);
	}

	cofactor(m0, m1, m2, m3) {
		return m0 * m3 - m1 * m2;
	}

    determinant() {
        var c00 = this.cofactor(this._11, this._21, this._12, this._22);
        var c01 = this.cofactor(this._10, this._20, this._12, this._22);
        var c02 = this.cofactor(this._10, this._20, this._11, this._21);
        return this._00 * c00 - this._01 * c01 + this._02 * c02;
    }

    inverse() {
		var c00 = this.cofactor(this._11, this._21, this._12, this._22);
		var c01 = this.cofactor(this._10, this._20, this._12, this._22);
		var c02 = this.cofactor(this._10, this._20, this._11, this._21);

		var det = this._00 * c00 - this._01 * c01 + this._02 * c02;
		if (Math.abs(det) < 0.000001) {
			throw "determinant is too small";
		}
		
		var c10 = this.cofactor(this._01, this._21, this._02, this._22);
		var c11 = this.cofactor(this._00, this._20, this._02, this._22);
		var c12 = this.cofactor(this._00, this._20, this._01, this._21);

		var c20 = this.cofactor(this._01, this._11, this._02, this._12);
		var c21 = this.cofactor(this._00, this._10, this._02, this._12);
		var c22 = this.cofactor(this._00, this._10, this._01, this._11);

		var invdet = 1.0 / det;
		return new FastMatrix3(
			 c00 * invdet, -c01 * invdet,  c02 * invdet,
			-c10 * invdet,  c11 * invdet, -c12 * invdet,
			 c20 * invdet, -c21 * invdet,  c22 * invdet
		);
	}
}
