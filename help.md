# INMP441 trên Milk-V Duo 256M - Phân tích chi tiết

## 1. Tóm tắt vấn đề
**INMP441** là I2S digital microphone đòi hỏi:
- 1 I2S controller ở **Master mode** (sinh BCLK, LRCK)
- Khả năng **RX** (receive audio data)

**Milk-V Duo 256M** dùng **SG2002** (RISC-V) trong **QFN package** (package nhỏ, ít pins)

## 2. I2S Controllers trên SG2002 / CV181X

### Định nghĩa DTS (cv181x_base.dtsi)

| I2S | Địa chỉ | Capability | DMA | Mục đích |
|-----|---------|-----------|-----|---------|
| **i2s0** | 0x04100000 | **RX only** | DMA ch 0,1 (read) | Nội bộ → ADC (internal analog input) |
| **i2s1** | 0x04110000 | **TXRX** | DMA ch 2,3 (rx/tx) | **External I2S** (bị xoá trên QFN) |
| **i2s2** | 0x04120000 | **TXRX** | DMA ch 6,7 (rx/tx) | **External I2S** (bị xoá trên QFN) |
| **i2s3** | 0x04130000 | **TX only** | DMA ch 7 (write) | Nội bộ → DAC (internal analog output) |

### Chi tiết các node DTS

**i2s0 (Internal ADC RX)**
```
i2s0: i2s@04100000 {
    compatible = "cvitek,cv1835-i2s";
    reg = <0x0 0x04100000 0x0 0x2000>;
    clocks = <&i2s_mclk 0>;
    dmas = <&dmac 0 1 1>; /* read channel */
    dma-names = "rx";
    capability = "rx"; /* I2S0 connect to internal ADC as RX */
    mclk_out = "false";
};
```

**i2s1 (External, TXRX)**
```
i2s1: i2s@04110000 {
    compatible = "cvitek,cv1835-i2s";
    reg = <0x0 0x04110000 0x0 0x2000>;
    clocks = <&i2s_mclk 0>;
    dmas = <&dmac 2 1 1    /* read channel */
            &dmac 3 1 1>;  /* write channel */
    dma-names = "rx", "tx";
    capability = "txrx";
    mclk_out = "false";
};
```

**i2s2 (External, TXRX)**
```
i2s2: i2s@04120000 {
    compatible = "cvitek,cv1835-i2s";
    reg = <0x0 0x04120000 0x0 0x2000>;
    clocks = <&i2s_mclk 0>;
    dmas = <&dmac 6 1 1    /* read channel */
            &dmac 1 1 1>;  /* write channel */
    dma-names = "rx", "tx";
    capability = "txrx";
    mclk_out = "false";
};
```

**i2s3 (Internal DAC TX)**
```
i2s3: i2s@04130000 {
    compatible = "cvitek,cv1835-i2s";
    reg = <0x0 0x04130000 0x0 0x2000>;
    clocks = <&i2s_mclk 0>;
    dmas = <&dmac 7 1 1>; /* write channel */
    dma-names = "tx";
    capability = "tx"; /* I2S3 connect to internal DAC as TX */
    mclk_out = "true"; /* MCLK source cho ADC/DAC */
};
```

### I2S MCLK Configuration
```
i2s_mclk: i2s_mclk {
    compatible = "fixed-clock";
    clock-frequency = <24576000>;  /* 24.576 MHz */
    clock-output-names = "i2s_mclk";
    #clock-cells = <0x0>;
};

i2s_subsys {
    compatible = "cvitek,i2s_tdm_subsys";
    reg = <0x0 0x04108000 0x0 0x100>;
    master_base = <0x04110000>;  /* i2s1 is master for multi-I2S sync */
};
```

## 3. QFN Package Limitations

### Các node bị XOÁ trên cv181x_asic_qfn.dtsi

```
/ {
    /delete-node/ i2s@04110000;      /* i2s1 - EXTERNAL */
    /delete-node/ i2s@04120000;      /* i2s2 - EXTERNAL */
    /delete-node/ sound_ext1;        /* i2s1 soundcard */
    /delete-node/ sound_ext2;        /* i2s2 soundcard */
    /delete-node/ sound_PDM;         /* PDM digital mic support */
    /delete-node/ wifi-sd@4320000;   /* SD interface (không dùng) */
};
```

### Lý do xoá

| Component | Lý do |
|-----------|------|
| **i2s1, i2s2** | QFN package không expose external I2S pins ra header |
| **sound_PDM** | PDM microphone interface cũng không có pins |
| **sound_ext1, sound_ext2** | Sound card definitions cho các external I2S |

## 4. Phân tích Hardware Pins

### Pins trên Milk-V Duo 256M

Milk-V Duo 256M có **26 GPIO pins** từ 2 port (Porta, Porte)

**AUDIO I2S Pins đòi hỏi:**
- BCLK (Bit clock) - output từ master
- LRCK (Frame/LR clock) - output từ master  
- SDI (Serial Data In) - RX
- SDO (Serial Data Out) - TX
- MCLK (Master Clock) - optional output

**Status trên Duo 256M:**
- ❌ Không có I2S pins được expose trên GPIO header
- ✅ Chỉ có UART, SPI, GPIO, I2C pins
- ✅ ADC/DAC nội bộ qua i2s0 và i2s3

## 5. Giải pháp Khả dụng

### ❌ Không Khả dụng - Lý do

| Giải pháp | Tại sao không? |
|-----------|----------------|
| **INMP441 qua I2S master mode** | Không có external I2S controller với capability TXRX và master mode |
| **Enable i2s1/i2s2** | Xoá trong DTS → pins không expose → hardware không hỗ trợ |
| **PDM microphone** | PDM interface bị xoá, không có pins |
| **Analog mic via ADC** | ADC chỉ dùng cho internal DAC (i2s0) |

### ✅ Giải pháp Thay thế

1. **USB Audio Interface**
   - Kết nối USB soundcard/microphone tới USB port
   - Kernel hỗ trợ: `snd-usb-audio` driver
   - Dễ implement nhất

2. **GPIO Bit-bang I2S (Complex)**
   - Implement I2S slave mode qua GPIO
   - Yêu cầu master clock từ INMP441 (không hỗ trợ slave mode)
   - Không khả thi

3. **Sử dụng Internal ADC + External Amplifier**
   - Nếu chấp nhận analog input từ mic
   - Cần analog amp circuit
   - Có thể bypass INMP441

4. **Custom Hardware Mod**
   - Rebind i2s0 từ internal ADC sang external INMP441
   - Yêu cầu thay đổi pinmux
   - Có thể gây conflict với internal audio

## 6. Audio Driver Stack

### Kernel Audio Drivers (sound/soc/cvitek/)

- `cv1835_i2s.c` - I2S DAI driver
- `cv1835_i2s_subsys.c` - I2S subsystem management
- `cv181xadc.c` - ADC driver (i2s0)
- `cv181xdac.c` - DAC driver (i2s3)
- `cv1835pdm.c` - PDM driver (bị disable trên QFN)
- `dummy_codec.c` - Placeholder codec cho external I2S

### Soundcard Definitions (cv181x_base.dtsi)

```
sound_adc {
    compatible = "cvitek,cv182xa-adc";
    cvi,model = "CV182XA";
    cvi,card_name = "cv182xa_adc";
};

sound_dac {
    compatible = "cvitek,cv182xa-dac";
    cvi,model = "CV182XA";
    cvi,card_name = "cv182xa_dac";
};

sound_PDM {
    compatible = "cvitek,cv182x-pdm";
    cvi,model = "CV182X";
    cvi,card_name = "cv182x_internal_PDM";
};
```

## 7. Boot Logs Verification

Từ `dmesg` trên board:

```
cvitek-i2s 4100000.i2s: cvi_i2s_probe → i2s0 probed
cvitek-i2s 4130000.i2s: cvi_i2s_probe → i2s3 probed
cviteka-adc sound_adc → card 0 (pcmC0D0c) - internal ADC
cviteka-dac sound_dac → card 1 (pcmC1D0p) - internal DAC
```

**i2s1, i2s2 không appear** → /delete-node/ có hiệu lực

## 8. Kết luận

**Trạng thái:** ❌ **KHÔNG khả thi** - INMP441 không thể dùng direct I2S master mode trên Duo 256M QFN

**Root Cause:**
- SG2002 có 4 I2S controller, nhưng:
  - i2s0 = RX only, nối internal ADC
  - i2s1, i2s2 = TXRX nhưng bị disable (pins not exposed)
  - i2s3 = TX only, nối internal DAC
- Không có controller nào expose external khả năng **TXRX + Master mode**

**Khuyến nghị:**
1. Dùng **USB audio interface** (easiest)
2. Sử dụng **internal ADC** nếu chấp nhận analog mic
3. Nâng cấp lên Milk-V Duo (BGA package) có i2s1/i2s2 exposed

---

**Tệp DTS liên quan:**
- `/linux_5.10/arch/riscv/boot/dts/cvitek/cv181x_base.dtsi` - Base audio config
- `/linux_5.10/arch/riscv/boot/dts/cvitek/cv181x_asic_qfn.dtsi` - QFN-specific deletions
- `/linux_5.10/arch/riscv/boot/dts/cvitek/sg2002_milkv_duo256m_musl_riscv64_sd.dts` - Board config