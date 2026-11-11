
class BleOta extends EventTarget {

  // Service UUID
  static OTA_SERVICE_UUID  = 0x8018;

  // Characteristic UUID
  static RECV_FW_CHAR_UUID = 0x8020;
  static OTA_BAR_CHAR_UUID = 0x8021;
  static COMMAND_CHAR_UUID = 0x8022;
  static CUSTOM_CHAR_UUID  = 0x8023;


  // Commands
  static CMD_ID__START = 0x0001;
  static CMD_ID__STOP  = 0x0002;
  static CMD_ID__ACK   = 0x0003;

  // Acknowledgements
  static CMD_ACK__ACCEPT = 0x0000;
  static CMD_ACK__REJECT = 0x0001;

  // FW ACK Status codes
  static ACK_STATUS__SUCCESS         = 0x0000;
  static ACK_STATUS__CRC_ERR         = 0x0001;
  static ACK_STATUS__SECTOR_IDX_ERR  = 0x0002;
  static ACK_STATUS__PAYLOAD_LEN_ERR = 0x0003;

  static OTA_BLOCK_SIZE = 4096;
  static OTA_BUFFER_SIZE = BleOta.OTA_BLOCK_SIZE + 2;

  static BLE_MTU = 256;

  // How many times one sector is sent when the device says that the data was damaged
  static SECTOR_TRIES = 3;

  // Time to wait for an answer of the device
  static INDICATION_TIMEOUT_MS = 5000;

  // Reconnect: first wait, longest wait between two tries, and when to stop
  static RECONNECT_FIRST_MS = 1000;
  static RECONNECT_MAX_MS = 4000;
  static RECONNECT_GIVE_UP_MS = 5 * 60 * 1000;


  // Custom Commands
  static CUSTOM_ID__GET_VERSION = 0x0001;
  static CUSTOM_ID__GET_UID     = 0x0002;
  static CUSTOM_ID__ACCEPT_FW   = 0x0003;
  static CUSTOM_ID__REJECT_FW   = 0x0004;
  static CUSTOM_ID__REBOOT      = 0x0005;



  constructor (device) {
    super();
    this.device = device;

    // False after disconnect(): the page does not connect again by itself
    this.wantConnection = true;
    this.connecting = false;
    // True while the connection is made and the characteristics are found
    this.ready = false;

    // On Disconnect
    device.addEventListener('gattserverdisconnected', () => {
      this.onDisconnected();
    });

    this.connectRetry();
  }


  static async create() {
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ name: "StatTrak" }],
      optionalServices: [BleOta.OTA_SERVICE_UUID]
    });
    
    return new BleOta(device);
  }

  async connect() {
    const gattServer = await this.device.gatt.connect();
    if(!gattServer) {
      throw new Error('Failed to connect to GATT server');
    }
    if(!this.wantConnection) {
      // disconnect() was called while this connection was made
      this.device.gatt.disconnect();
      throw new Error('The connection is not wanted');
    }

    // Get Service
    const service = await gattServer.getPrimaryService(BleOta.OTA_SERVICE_UUID);
    if(!service) {
      throw new Error('Failed to get service');
    }

    // Get Characteristics
    const recvFwChar = await service.getCharacteristic(BleOta.RECV_FW_CHAR_UUID);
    const otaBarChar = await service.getCharacteristic(BleOta.OTA_BAR_CHAR_UUID);
    const commandChar = await service.getCharacteristic(BleOta.COMMAND_CHAR_UUID);
    const customChar = await service.getCharacteristic(BleOta.CUSTOM_CHAR_UUID);

    if(!recvFwChar || !otaBarChar || !commandChar || !customChar) {
      throw new Error('Failed to get characteristics');
    }

    console.log("Connected to device:", this.device.name);
    
    this.recvFwChar = recvFwChar;
    this.otaBarChar = otaBarChar;
    this.commandChar = commandChar;
    this.customChar = customChar;
    this.ready = true;

    this.dispatchEvent(new CustomEvent('bleConnected', { detail: { device: this.device } }));

    return gattServer;
  }

  /* Tries to connect until it works. It stops after disconnect(), or after
     RECONNECT_GIVE_UP_MS (then it sends the event 'bleConnectFailed').
     After a firmware transfer the device restarts, and a person must sometimes
     switch it on again. Thus the tries continue for some minutes. */
  async connectRetry() {
    if(this.connecting)
      return;
    this.connecting = true;
    this.wantConnection = true;

    let delay = BleOta.RECONNECT_FIRST_MS;
    const deadline = Date.now() + BleOta.RECONNECT_GIVE_UP_MS;

    while(this.wantConnection && !this.ready) {
      try {
        time('Connecting to Bluetooth Device... ');
        await this.connect();
        console.log('> Bluetooth Device connected!');
        break;
      } catch(error) {
        if(!this.wantConnection)
          break;
        if(Date.now() >= deadline) {
          time('Failed to connect.');
          this.wantConnection = false;
          this.dispatchEvent(new CustomEvent('bleConnectFailed', { detail: { device: this.device } }));
          break;
        }
        await new Promise((resolve) => setTimeout(resolve, delay));
        delay = Math.min(delay * 2, BleOta.RECONNECT_MAX_MS);
      }
    }

    this.connecting = false;
  }


  onDisconnected() {
    console.log('> Bluetooth Device disconnected:', this.device.name);
    this.ready = false;
    this.dispatchEvent(new CustomEvent('bleDisconnected', { detail: { device: this.device } }));
    if(this.wantConnection)
      this.connectRetry();
  }

  /* Disconnects, and does not connect again until connectRetry() is called. */
  disconnect() {
    this.wantConnection = false;
    // This also stops a connection try that is in progress
    if(this.device.gatt)
      this.device.gatt.disconnect();
  }

  isConnected() {
    return !!(this.device.gatt && this.device.gatt.connected);
  }


  async writeAndWaitForIndication(characteristic, data) {
    return await this.writeAndWaitForIndicationWithoutStop(characteristic, data).then(async (value) => {
      try {
        await characteristic.stopNotifications();
      } catch (e) {
        console.warn('Failed to stop notifications:', e);
      }

      return value;
    });
  }

  

  async writeAndWaitForIndicationWithoutStop(characteristic, data) {
    return new Promise(async (resolve, reject) => {
      let timeoutId;
  
      function onIndication(event) {
        clearTimeout(timeoutId);
        characteristic.removeEventListener('characteristicvaluechanged', onIndication);
        resolve(event.target.value);  // the indication payload
      }
  
      try {
        // 1. Make sure notifications/indications are enabled

        await characteristic.startNotifications();
  
        // 2. Attach listener before writing (to avoid missing quick indications)
        characteristic.addEventListener('characteristicvaluechanged', onIndication);
  
        // 3. Perform the write
        await characteristic.writeValueWithResponse(data);
  
        // 4. Set a timeout so we don’t wait forever
        timeoutId = setTimeout(() => {
          characteristic.removeEventListener('characteristicvaluechanged', onIndication);
          reject(new Error('Timeout waiting for indication'));
        }, BleOta.INDICATION_TIMEOUT_MS);
      } catch (err) {
        reject(err);
      }
    });
  }

  async sendCmd(cmdId, payload) {
    if(this.device.gatt.connected) {
      let cmd = new Uint8Array(20);
      let view = new DataView(cmd.buffer);
    
      // Ensure that payload is 16 bytes long, else append zeros
      if(payload.length < 16) {
        payload = new Uint8Array([...payload, ...new Array(16 - payload.length).fill(0)]);
      } else if(payload.length > 16) {
        // Truncate payload if longer than 16 bytes
        payload = payload.slice(0, 16);
      }
    
      view.setUint16(0, cmdId, true);
      cmd.set(payload, 2);
      view.setUint16(18, crc16_ccitt(cmd.slice(0, 18)), true);
    
      // let resp = await this.commandChar.writeValueWithResponse(cmd);
      // console.log('Response: ' + resp);

      console.log("Sending command:", cmd);

      let resp = await this.writeAndWaitForIndication(this.commandChar, cmd);
      if(resp) {
        let respView = new DataView(resp.buffer);

        console.log('Received response:', resp.buffer);

        let ackCmdId = respView.getUint16(0, true);
        let ackRespCmdId = respView.getUint16(2, true);
        let ackStatus = respView.getUint16(4, true);
        let ackCrc = respView.getUint16(18, true);

        console.log('Response:', {
          ackCmdId,
          ackRespCmdId,
          ackStatus,
          ackCrc
        });


        // Check CRC
        const expectedCrc = crc16_ccitt(new Uint8Array(resp.buffer.slice(0, 18)));
        if(ackCrc !== expectedCrc) {
          console.error('CRC mismatch in response', ackCrc, "!=", expectedCrc);
          return false;
        }

        // Check command ID && response command ID
        if(ackCmdId !== BleOta.CMD_ID__ACK) {
          console.error('Unexpected command ID in response:', ackCmdId);
          return false;
        }

        // Check response command ID
        if(ackRespCmdId !== cmdId) {
          console.error('Response command ID does not match sent command ID:', ackRespCmdId, "!=", cmdId);
          return false;
        }

        // Check status
        if(ackStatus !== BleOta.CMD_ACK__ACCEPT) {
          console.error('Command rejected with status:', ackStatus);
          return false;
        }

        console.log('Command accepted:', cmdId);

        return true;
      } else {
        console.error('No response received for command:', cmdId);
        return false;
      }
    }

    console.error('Device not connected');
    return false;
  }


  async sendCustomCmd(cmdId, payload) {
    if(this.device.gatt.connected) {
      let cmd = new Uint8Array(20);
      let view = new DataView(cmd.buffer);

      // Ensoure that payload is 16 bytes long, else append zeros
      if(payload.length < 16) {
        payload = new Uint8Array([...payload, ...new Array(16 - payload.length).fill(0)]);
      } else if(payload.length > 16) {
        payload = payload.slice(0, 16);
      }

      // Construct command
      view.setUint16(0, cmdId, true);
      cmd.set(payload, 2);
      view.setUint16(18, crc16_ccitt(cmd.slice(0, 18)), true);

      // return this.writeAndWaitForIndication(this.customChar, cmd);
      let response = await this.writeAndWaitForIndication(this.customChar, cmd);

      if(response && response.byteLength >= 20) {
        let respView = new DataView(response.buffer);

        let ackCmdId = respView.getUint16(0, true);
        let ackRespCmdId = respView.getUint16(2, true);
        let ackCrc = respView.getUint16(18, true);
        let respPayload = new Uint8Array(response.buffer.slice(4, 18));

        // Check CRC
        const expectedCrc = crc16_ccitt(new Uint8Array(response.buffer.slice(0, 18)));
        if(ackCrc !== expectedCrc) {
          console.error('CRC mismatch in response', ackCrc, "!=", expectedCrc);
          return null;
        }

        // The answer must be an ACK for the command that was sent
        if(ackCmdId !== BleOta.CMD_ID__ACK || ackRespCmdId !== cmdId) {
          console.error('No ACK or unexpected response CMD ID', ackCmdId, ackRespCmdId);
          return null;
        }

        return respPayload;
      }

      return Promise.reject(new Error('Invalid response'));
    }

    return Promise.reject(new Error('Device not connected'));
  }


  /* Sends the firmware. Each sector is OTA_BLOCK_SIZE bytes (the last one is filled
     with zeros) plus the CRC16 of its firmware bytes. The device answers each sector. */
  async sendFwData(data) {
    if(!this.device.gatt.connected) {
      console.error('Device not connected');
      return false;
    }

    const dataLength = data.length;
    const sectorCount = Math.ceil(dataLength / BleOta.OTA_BLOCK_SIZE);
    let progress = 0;

    for(let sector = 0; sector < sectorCount; sector++) {
      const fwBytes = data.slice(sector * BleOta.OTA_BLOCK_SIZE, (sector + 1) * BleOta.OTA_BLOCK_SIZE);
      const crc = crc16_ccitt(fwBytes);
      const sectorData = new Uint8Array(BleOta.OTA_BUFFER_SIZE);
      sectorData.set(fwBytes);
      sectorData.set(new Uint8Array([(crc & 0xFF), (crc >> 8)]), BleOta.OTA_BLOCK_SIZE);

      let status = null;
      for(let attempt = 1; attempt <= BleOta.SECTOR_TRIES; attempt++) {
        status = await this.sendSector(sector, sectorData);
        if(status === BleOta.ACK_STATUS__SUCCESS)
          break;

        /* The device found damaged data and did not use this sector. It waits for the
           same sector again. Each other answer means that the two sides do not agree
           about the position in the file: the transfer cannot continue. */
        if(status !== BleOta.ACK_STATUS__CRC_ERR && status !== BleOta.ACK_STATUS__PAYLOAD_LEN_ERR) {
          console.error('Sector transfer failed with status:', status);
          return false;
        }
        console.warn(`Sector ${sector} was damaged (status ${status}), try ${attempt} of ${BleOta.SECTOR_TRIES}`);
      }

      if(status !== BleOta.ACK_STATUS__SUCCESS) {
        console.error('Sector transfer failed after', BleOta.SECTOR_TRIES, 'tries:', sector);
        return false;
      }

      console.log('Sector transfer successful:', sector);

      // print progress
      const sent = Math.min((sector + 1) * BleOta.OTA_BLOCK_SIZE, dataLength);
      const newProgress = Math.floor(sent / dataLength * 100);
      if(newProgress !== progress) {
        progress = newProgress;
        console.log(`Progress: ${progress}%`);
        this.dispatchEvent(new CustomEvent('progress', { detail: { progress } }));
      }
    }

    // finished
    console.log('Firmware data sent successfully');
    return true;
  }


  /* Sends one sector in packets: [sector u16][packet number u8][data].
     The last packet has the number 255, and the device answers it.
     Returns the status of the answer (ACK_STATUS__*). */
  async sendSector(sector, sectorData) {
    const DATA_PER_PACKET = BleOta.BLE_MTU - 4 - 3;

    /* The packet number 255 is the mark of the last packet,
       thus a sector must fit in less than 255 packets */
    if(sectorData.length / DATA_PER_PACKET > 255) {
      throw new Error('OTA_BLOCK_SIZE is too big');
    }

    let packetSeq = 0;
    for(let offset = 0; offset < sectorData.length; offset += DATA_PER_PACKET) {
      const payload = sectorData.slice(offset, offset + DATA_PER_PACKET);
      const lastPacket = (offset + DATA_PER_PACKET >= sectorData.length);

      const packet = new Uint8Array(3 + payload.length);
      const packetView = new DataView(packet.buffer);
      packetView.setUint16(0, sector, true);                    // Sector index
      packetView.setUint8(2, lastPacket ? 255 : packetSeq);     // Packet sequence number
      packet.set(payload, 3);

      if(!lastPacket) {
        await this.recvFwChar.writeValueWithResponse(packet);
        packetSeq++;
        continue;
      }

      const resp = await this.writeAndWaitForIndication(this.recvFwChar, packet);
      if(!resp) {
        throw new Error('No response received for sector ' + sector);
      }

      const respBytes = new Uint8Array(resp.buffer, resp.byteOffset, resp.byteLength);
      const respView = new DataView(respBytes.buffer, respBytes.byteOffset, respBytes.byteLength);

      const ackSector = respView.getUint16(0, true);
      const ackStatus = respView.getUint16(2, true);
      const ackCrc = respView.getUint16(18, true);

      // Check CRC
      if(ackCrc !== crc16_ccitt(respBytes.slice(0, 18))) {
        throw new Error('CRC mismatch in the answer for sector ' + sector);
      }

      // The answer is for an other sector: the device is at an other position
      if(ackSector !== sector) {
        console.error('Unexpected sector index in response:', ackSector);
        return BleOta.ACK_STATUS__SECTOR_IDX_ERR;
      }

      return ackStatus;
    }

    throw new Error('Empty sector');
  }


  async getVersion() {
    let response = await this.sendCustomCmd(BleOta.CUSTOM_ID__GET_VERSION, new Uint8Array(16).fill(0));

    if(response) {
      // The text ends at the first zero byte
      let end = response.indexOf(0);
      let versionString = new TextDecoder().decode(response.slice(0, end < 0 ? response.length : end));
      console.log('Version Response:', versionString);

      return versionString;
    } else {
      console.error('Failed to get version');

      return null;
    }
  }

  async getUid() {
    let response = await this.sendCustomCmd(BleOta.CUSTOM_ID__GET_UID, new Uint8Array(16).fill(0));

    if(response) {
      let uid = new Uint8Array(response.buffer.slice(0, 6));
      console.log('UID Response:', uid);

      return uid;
    } else {
      console.error('Failed to get UID');

      return null;
    }
  }


  async acceptFirmware(accept) {
    let cmdId = accept ? BleOta.CUSTOM_ID__ACCEPT_FW : BleOta.CUSTOM_ID__REJECT_FW;
    let response = await this.sendCustomCmd(cmdId, new Uint8Array(16).fill(0));

    if(response) {
      let status = response[0];
      console.log('Firmware acceptance response:', status);

      return status === 1; // 1 for accepted, 0 for rejected
    } else {
      // No valid answer is not the same as the answer "no"
      throw new Error('No valid answer to the firmware confirmation');
    }
  }



  async update(data) {

    // Send command to start OTA
    let payload = new Uint8Array(16);
    let view = new DataView(payload.buffer);
    view.setUint32(0, data.length, true);
    let sent = false;
    try {
      if(await this.sendCmd(BleOta.CMD_ID__START, payload)) {
        // Send firmware data
        sent = await this.sendFwData(data);
      } else {
        console.error('Failed to start OTA');
      }
    } finally {
      /* Also when the answer to "start" did not arrive: the device possibly
         started, and it must not wait for data that does not come. */
      if(!sent) {
        console.error('Failed to send firmware data');
        await this.stop();
      }
    }

    return sent;
  }


  /* Tells the device that the transfer is stopped. Without this the device
     waits 15 seconds for more data before it shows "Error". */
  async stop() {
    if(!this.isConnected())
      return false;

    try {
      return await this.sendCmd(BleOta.CMD_ID__STOP, new Uint8Array(16));
    } catch(error) {
      console.warn('Failed to send the stop command:', error);
      return false;
    }
  }

}