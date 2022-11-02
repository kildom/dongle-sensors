
const SERVICE_UUID = "cc2af14a-2aaf-4c6e-b2e4-3856ee2b4267";
const CHAR_UUID = "45cc8e0b-8507-45f7-ac95-b798d0fd732a";

const MAX_REQUEST_SIZE = 512;
const CHUNK_SIZE = 16;
const CHUNK_FLAG_BEGIN = 0x40;
const CHUNK_FLAG_END = 0x80;
const CHUNK_ID_MASK = 0x3F;

const CMD_GET_UP_TIME = 1;
const CMD_READ = 2;
const CMD_WRITE = 3;
const CMD_KEEP = 4;

const STATUS_OK = 0;
const STATUS_UNKNOWN_CMD = 1;
const STATUS_OUT_OF_BOUNDS = 2;

const MEMORY_CONFIG = 0;
const MEMORY_STATE = 1;

let device = null;
let service;
let characteristic = null;
let lastCommandId = 0;
let connectionCounter = 0;

async function bleConnect()
{
	connectionCounter++;

	console.log('Requesting Bluetooth Device...');
	device = await navigator.bluetooth.requestDevice({
		filters: [{ services: [SERVICE_UUID] }],
		optionalServices: [SERVICE_UUID]
	});

	console.log('Connecting to Bluetooth Device...');
	await device.gatt.connect();

	console.log('Requesting service...');
	service = await device.gatt.getPrimaryService(SERVICE_UUID);

	console.log('Requesting characteristic...');
	characteristic = await service.getCharacteristic(CHAR_UUID);

	console.log('Ready to communicate');
}

async function bleCommandInner(data) {
	lastCommandId = (lastCommandId + 1) & CHUNK_ID_MASK;
	let offset = 0;
	if (data.length == 0 || data.length > MAX_REQUEST_SIZE) {
		throw Error('Invalid command length');
	}
	while (offset < data.length) {
		let writeBytes = data.length - offset;
		if (writeBytes > CHUNK_SIZE) {
			writeBytes = CHUNK_SIZE;
		}
		let temp = new Uint8Array(1 + writeBytes);
		temp[0] = lastCommandId;
		if (offset == 0) {
			temp[0] |= CHUNK_FLAG_BEGIN;
		}
		if (offset + writeBytes == data.length) {
			temp[0] |= CHUNK_FLAG_END;
		}
		console.log(`Writing ${writeBytes} of ${data.length} to ${offset}`);
		temp.set(data.subarray(offset, offset + writeBytes), 1);
		await characteristic.writeValueWithResponse(temp);
		offset += writeBytes;
	}

	let result = new Uint8Array(0);
	do {
		buf = await characteristic.readValue();
		buf = new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
		if (buf.length < 1) {
			await new Promise(resolve => setTimeout(resolve, 200));
			continue;
		} else if ((buf[0] & CHUNK_ID_MASK) != lastCommandId) {
			throw Error('Invalid response - unexpected id');
		} else if (result.length == 0 && !(buf[0] & CHUNK_FLAG_BEGIN)) {
			throw Error('Invalid response - expecting response begin');
		}
		console.log(`Read ${buf.length - 1} to ${result.length}`);
		let t = new Uint8Array(result.length + buf.length - 1);
		t.set(result);
		t.set(buf.subarray(1), result.length);
		result = t;
	} while (!(buf[0] & CHUNK_FLAG_END));

	console.log(`Total read: ${result.length}`);

	return result;
}

let bleCommandActive = false;
let bleCommandQueue = [];

async function bleCommand(data) {
	if (bleCommandActive) {
		await new Promise(resolve => bleCommandQueue.push(resolve));
	}
	bleCommandActive = true;
	let retryCounter = 11;
	try {
		do {
			if (retryCounter == 0) {
				return await bleCommandInner(data);
			} else {
				try {
					return await bleCommandInner(data);
				} catch (ex) { }
				connectionCounter++;
				retryCounter--;
				try {
					if (retryCounter <= 8) {
						device.gatt.disconnect();
						await new Promise(r => setTimeout(r, 1000 * (8 - retryCounter)));
					}
					if (retryCounter >= 7) {
						await device.gatt.connect();
						service = await device.gatt.getPrimaryService(SERVICE_UUID);
						characteristic = await service.getCharacteristic(CHAR_UUID);
					} else if (retryCounter >= 5) {
						// TODO: show popup to confirm reconnection, because bleConnect() must be called by the user event
						await bleConnect();
					}
				} catch (ex) { }
			}
		} while (true);
	} finally {
		if (bleCommandQueue.length > 0) {
			setTimeout(() => {
				let resolve = bleCommandQueue.shift();
				resolve();
			}, 0);
		} else {
			bleCommandActive = false;
		}
	}
}

function createCommand(cmd, requestDataSize = 0, tag = 0)
{
	let id = Math.floor(Math.random() * 65535);
	let ab = new ArrayBuffer(4 + requestDataSize);
	let header = new DataView(ab);
	header.setUint8(0, cmd);
	header.setUint8(1, tag);
	header.setUint16(2, id, true);
	return new DataView(ab, 4);
}

async function execCommand(data)
{
	let ab = data.buffer;
	let responseArray = await bleCommand(new Uint8Array(ab));
	let requestHeader = new DataView(ab);
	let responseHeader = new DataView(responseArray.buffer, responseArray.byteOffset, responseArray.byteLength);
	if (responseHeader.getUint8(0) != requestHeader.getUint8(0)
		|| responseHeader.getUint16(2, true) != requestHeader.getUint16(2, true)) {
		throw Error('Command out of sync!');
	}
	if (responseHeader.getUint8(1) != STATUS_OK) {
		throw Error(`Response status ${responseHeader.getUint8(1)}`);
	}
	return responseArray.subarray(4);
}

async function getUpTime() {
	let c = createCommand(CMD_GET_UP_TIME);
	let buf = await execCommand(c);
	let dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
	return dv.getUint32(0, true);
}

async function readMemory(memory, offset, size) {
	let c = createCommand(CMD_READ, 4, memory);
	c.setUint16(0, offset, true);
	c.setUint16(2, size, true);
	return await execCommand(c);
}

async function writeMemory(memory, offset, data) {
	let length = data.byteLength;
	let c = createCommand(CMD_WRITE, 2 + length, memory);
	c.setUint16(0, offset, true);
	let dst = new Uint8Array(c.buffer, c.byteOffset + 2, c.byteLength - 2);
	dst.set(new Uint8Array(data.buffer, data.byteOffset, length));
	await execCommand(c);
}

class Container {
	constructor(parent, align) {
		align = align || 1;
		this._parent = parent;
		this._align = align;
		this._children = [];
		this._arrayBuffer = null;
		this._requireEndOffset = null;
		if (parent) {
			this._startOffset = parent._endOffset;
			let unalign = this._startOffset % align;
			if (unalign != 0) {
				this._startOffset += align - unalign;
			}
			this._endOffset = this._startOffset;
			parent._children.push(this);
		} else {
			this._startOffset = 0;
			this._endOffset = 0;
		}
	}
	_endOfContainer(memory) {
		let unalign = this._endOffset % this._align;
		if (unalign != 0) {
			this._endOffset += this._align - unalign;
		}
		if (this._requireEndOffset === null) {
			this._requireEndOffset = this._endOffset;
		}
		if (this._parent) {
			this._parent._endOffset = this._endOffset;
		} else {
			this._propagateArrays(
				new ArrayBuffer(this._endOffset),
				(new Array(this._endOffset + 1)).fill(false),
				memory);
		}
	}
	_endOfRequireZone() {
		this._requireEndOffset = this._endOffset;
	}
	_propagateArrays(buffer, flags, memory)
	{
		this._arrayBuffer = buffer;
		this._loadedFlags = flags;
		this._memory = memory;
		for (let c of this._children) {
			c._propagateArrays(buffer, flags, memory);
		}
		this._setArrays(buffer, flags, memory);
	}

	_setArrays(buffer, flags, memory) {
	}

	async require(allowCache, start, end) {
		if (start === undefined) {
			start = this._startOffset;
			end = this._requireEndOffset;
		}
		if (connectionCounter !== this._loadedFlags[this._loadedFlags.length - 1]) {
			this._loadedFlags.fill(false);
			this._loadedFlags[this._loadedFlags.length - 1] = connectionCounter;
			allowCache = false;
		} else if (allowCache) {
			for (let i = start; i < end; i++) {
				if (!this._loadedFlags[i]) {
					allowCache = false;
					break;
				}
			}
		}
		if (!allowCache) {
			let buffer = await readMemory(this._memory, start, end - start);
			new Uint8Array(this._arrayBuffer).set(buffer, start)
			for (let i = start; i < end; i++) {
				this._loadedFlags[i] = true;
			}
		}
	}

	async update(allowCache, start, end) {
		if (start === undefined) {
			start = this._startOffset;
			end = this._requireEndOffset;
		}
		for (let i = start; i < end; i++) {
			this._loadedFlags[i] = true;
		}
		if (!allowCache) {
			await writeMemory(this._memory, start, new Uint8Array(this._arrayBuffer, start, end - start));
		}
	}
}

class BasicType extends Container {
	constructor(parent, size, align) {
		align = align || size;
		super(parent, align);
		this._endOffset += size;
		this._endOfContainer();
	}
}

class IntType extends BasicType {
	constructor(parent, Type) {
		super(parent, Type.BYTES_PER_ELEMENT);
		this._Type = Type;
	}
	_setArrays(buffer) {
		this._data = new this._Type(buffer, this._startOffset, 1);
	}
	async get(allowCache) {
		await this.require(allowCache);
		return this._data[0];
	}
	async set(value, allowCache) {
		this._data[0] = value;
		await this.update(allowCache);
	}
}

class C_uint8_t extends IntType {
	constructor(parent) {
		super(parent, Uint8Array);
	}
}

class C_int8_t extends IntType {
	constructor(parent) {
		super(parent, Int8Array);
	}
}

class C_uint16_t extends IntType {
	constructor(parent) {
		super(parent, Uint16Array);
	}
}

class C_int16_t extends IntType {
	constructor(parent) {
		super(parent, Int16Array);
	}
}

class FixedDecimal16 extends C_int16_t {
	constructor(parent) {
		super(parent);
	}
	async get(allowCache) {
		let r = await super.get(allowCache);
		if (r == 0x7FFF) {
			return NaN;
		} else {
			return r / 100;
		}
	}
	async set(value, allowCache) {
		let x = Math.round(value * 100);
		if (x >= 0x7FFF) {
			x = 0x7FFE;
		} else if (x <= -0x7FFF) {
			x = -0x7FFE;
		}
		await super.set(x, allowCache);
	}
}

class C_uint32_t extends IntType {
	constructor(parent) {
		super(parent, Uint32Array);
	}
}

class C_int32_t extends IntType {
	constructor(parent) {
		super(parent, Int32Array);
	}
}

class C_string extends BasicType {
	constructor(parent, length) {
		super(parent, length, 1);
		this._length = length;
	}
	_setArrays(buffer) {
		this._data = new Uint8Array(buffer, this._startOffset, this._length);
	}
	async get(allowCache) {
		await this.require(allowCache);
		let str = new TextDecoder().decode(this._data);
		let pos = str.indexOf('\0');
		if (pos < 0) return str;
		return str.substring(0, pos);
	}
	async set(value, allowCache) {
		let buf;
		do {
			buf = new TextEncoder().encode(value);
			if (buf.length + 1 > this._length) {
				value = value.substring(0, value.length - 1);
			}
		} while (buf.length + 1 > this._length);
		this._data.fill(0);
		this._data.set(buf);
		await this.update(allowCache);
	}
}

class DaylightTransition extends Container {
	constructor(parent) {
		super(parent, 2);
		this.time = new C_int16_t(this);
		this.month = new C_int8_t(this);
		this.day = new C_int8_t(this);
		this.week = new C_int8_t(this);
		this._endOfContainer();
	}
}

class TimeZone extends Container {
	constructor(parent) {
		super(parent, 2);
		this.utc_offset = new C_int16_t(this);
		this.daylight_delta = new C_int16_t(this);
		this.daylight_start = new DaylightTransition(this);
		this.daylight_end = new DaylightTransition(this);
		this._endOfContainer();
	}
}

class ConfigNode extends Container {
	constructor(parent) {
		super(parent, 4);
		this.addr_low = new C_uint32_t(this);
		this.addr_high = new C_uint16_t(this);
		this.channel = new C_uint8_t(this);
		this.name = new C_string(this, 48);
		this._endOfContainer();
	}
}

class ConfigChannel extends Container {
	constructor(parent) {
		super(parent, 1);
		this.func = new C_uint8_t(this);
		this.name = new C_string(this, 48);
		this._endOfContainer();
	}
}

class Config extends Container {
	constructor() {
		super(null, 4);
		this.config_version = new C_uint8_t(this);
		this.node_count = new C_uint8_t(this);
		this.channel_count = new C_uint8_t(this);
		this._reserved_1 = new C_uint8_t(this);
		this.time_zone = new TimeZone(this);
		this._endOfRequireZone();
		this.nodes = (new Array(32)).fill().map(_ => new ConfigNode(this));
		this.channels = (new Array(8)).fill().map(_ => new ConfigChannel(this));
		this._endOfContainer(MEMORY_CONFIG);
	}
}

class StateNode extends Container {
	constructor(parent) {
		super(parent, 4);
		this.last_update = new C_uint32_t(this);
		this.temperature = new FixedDecimal16(this);
		this.voltage = new FixedDecimal16(this);
		this._endOfContainer();
	}
}

class StateChannel extends Container {
	constructor(parent) {
		super(parent, 2);
		this.temperature = new FixedDecimal16(this);
		this._endOfContainer();
	}
}

class State extends Container {
	constructor() {
		super(null, 4);
		this.time_shift = new C_uint32_t(this);
		this._endOfRequireZone();
		this.nodes = (new Array(32)).fill().map(_ => new StateNode(this));
		this.channels = (new Array(8)).fill().map(_ => new StateChannel(this));
		this._endOfContainer(MEMORY_STATE);
	}
}

async function main() {
	await bleConnect();
	console.log(await getUpTime());
	let config = new Config();
	let state = new State();
	console.log(config);
	await config.require(true);
	/*console.log(await config.node_count.get(true));
	console.log(await config.channel_count.get(true));
	await config.nodes[0].require(true);
	console.log(await config.nodes[0].addr_high.get(true));
	console.log(await config.nodes[0].addr_low.get(true));
	console.log(await config.nodes[0].name.get(true));
	await config.nodes[0].name.set("Thi453545!", true);
	console.log(await config.nodes[0].name.get(true));
	await config.nodes[0].update();*/
	let t = await getUpTime();
	let now = Math.round(Date.now() / 1000);
	let shift = now - t;
	await state.time_shift.set(shift);
	let f = async () => {
		await state.nodes[0].require();
		console.log('temperature', await state.nodes[0].temperature.get(true));
		console.log('voltage', await state.nodes[0].voltage.get(true));
		let t = await state.nodes[0].last_update.get(true) + await state.time_shift.get();
		console.log('last update', new Date(t * 1000));
		console.log('---', Math.round(Date.now() / 1000) - t);
		setTimeout(f, 1000);
	};
	f();
	let f2 = async () => {
		await state.nodes[1].require();
		console.log('2 temperature', await state.nodes[1].temperature.get(true));
		console.log('2 voltage', await state.nodes[1].voltage.get(true));
		let t = await state.nodes[1].last_update.get(true) + await state.time_shift.get();
		console.log('2 last update', new Date(t * 1000));
		console.log('2 ---', Math.round(Date.now() / 1000) - t);
		setTimeout(f2, 1000);
	};
	f2();
}

/*
Voltage to battery level:
	if v < 275 return 0;
	if v > 300 return 100;
	return (32 * v * v - 17412 * v + 2368665) >> 8;
*/