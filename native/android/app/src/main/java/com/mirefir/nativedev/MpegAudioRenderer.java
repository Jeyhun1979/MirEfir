package com.mirefir.nativedev;

import android.os.Handler;
import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.decoder.CryptoConfig;
import androidx.media3.decoder.DecoderException;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.decoder.SimpleDecoder;
import androidx.media3.decoder.SimpleDecoderOutputBuffer;
import androidx.media3.exoplayer.audio.AudioRendererEventListener;
import androidx.media3.exoplayer.audio.AudioSink;
import androidx.media3.exoplayer.audio.DecoderAudioRenderer;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import javazoom.jl.decoder.Bitstream;
import javazoom.jl.decoder.Decoder;
import javazoom.jl.decoder.Header;
import javazoom.jl.decoder.SampleBuffer;

@UnstableApi
final class MpegAudioRenderer extends DecoderAudioRenderer<MpegAudioRenderer.MpegDecoder> {
  MpegAudioRenderer(@Nullable Handler eventHandler, @Nullable AudioRendererEventListener eventListener, AudioSink audioSink) {
    super(eventHandler, eventListener, audioSink);
  }

  @Override
  public String getName() {
    return "MpegAudioRenderer";
  }

  @Override
  protected @C.FormatSupport int supportsFormatInternal(Format format) {
    String mime = format.sampleMimeType;
    if (mime == null) return C.FORMAT_UNSUPPORTED_TYPE;
    if (MimeTypes.AUDIO_MPEG_L1.equals(mime) || MimeTypes.AUDIO_MPEG_L2.equals(mime) || "audio/mp2".equals(mime)) {
      return C.FORMAT_HANDLED;
    }
    return C.FORMAT_UNSUPPORTED_TYPE;
  }

  @Override
  protected MpegDecoder createDecoder(Format format, @Nullable CryptoConfig cryptoConfig) {
    return new MpegDecoder(format);
  }

  @Override
  protected Format getOutputFormat(MpegDecoder decoder) {
    return decoder.outputFormat();
  }

  static final class MpegDecoder extends SimpleDecoder<DecoderInputBuffer, SimpleDecoderOutputBuffer, DecoderException> {
    private Decoder decoder;
    private Bitstream bitstream;
    private final FillStream fill = new FillStream();
    private int sampleRate;
    private int channels;
    private byte[] pending = new byte[0];

    MpegDecoder(Format format) {
      super(new DecoderInputBuffer[4], new SimpleDecoderOutputBuffer[4]);
      setInitialInputBufferSize(8192);
      sampleRate = format != null && format.sampleRate > 0 ? format.sampleRate : 48000;
      channels = format != null && format.channelCount > 0 ? format.channelCount : 2;
    }

    @Override
    public String getName() {
      return "MpegDecoder";
    }

    Format outputFormat() {
      return new Format.Builder()
          .setSampleMimeType(MimeTypes.AUDIO_RAW)
          .setPcmEncoding(C.ENCODING_PCM_16BIT)
          .setChannelCount(Math.max(1, channels))
          .setSampleRate(Math.max(8000, sampleRate))
          .build();
    }

    @Override
    protected DecoderInputBuffer createInputBuffer() {
      return new DecoderInputBuffer(DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_NORMAL);
    }

    @Override
    protected SimpleDecoderOutputBuffer createOutputBuffer() {
      return new SimpleDecoderOutputBuffer(this::releaseOutputBuffer);
    }

    @Override
    protected DecoderException createUnexpectedDecodeException(Throwable error) {
      return new DecoderException("mpeg", error);
    }

    @Override
    protected DecoderException decode(DecoderInputBuffer input, SimpleDecoderOutputBuffer output, boolean reset) {
      if (reset) {
        closeBitstream();
        decoder = null;
        pending = new byte[0];
        fill.clear();
      }
      ByteBuffer data = input.data;
      if (data == null || (!data.hasRemaining() && pending.length == 0)) {
        output.init(input.timeUs, 0);
        return null;
      }
      byte[] chunk = new byte[data.remaining()];
      data.get(chunk);
      byte[] src = concat(pending, chunk);
      pending = new byte[0];
      int added = 0;
      int i = 0;
      while (i + 4 <= src.length) {
        int sync = findSync(src, i);
        if (sync < 0) {
          pending = slice(src, i, src.length);
          break;
        }
        int len = frameLen(src, sync);
        if (len < 24) {
          i = sync + 1;
          continue;
        }
        if (sync + len > src.length) {
          pending = slice(src, sync, src.length);
          break;
        }
        fill.add(src, sync, len);
        added++;
        i = sync + len;
      }
      if (added == 0) {
        output.init(input.timeUs, 0);
        return null;
      }
      if (decoder == null) {
        decoder = new Decoder();
        bitstream = new Bitstream(fill);
      }
      ByteBuffer pcm = ByteBuffer.allocate(Math.max(8192, added * 2304 * 2)).order(ByteOrder.LITTLE_ENDIAN);
      try {
        for (int f = 0; f < added; f++) {
          Header header = bitstream.readFrame();
          if (header == null) break;
          SampleBuffer samples = (SampleBuffer) decoder.decodeFrame(header, bitstream);
          bitstream.closeFrame();
          if (samples == null) continue;
          sampleRate = samples.getSampleFrequency();
          channels = Math.max(1, samples.getChannelCount());
          short[] buf = samples.getBuffer();
          int n = samples.getBufferLength();
          if (n > buf.length) n = buf.length;
          if (n < 0) n = 0;
          for (int s = 0; s < n; s++) pcm.putShort(buf[s]);
        }
      } catch (Exception e) {
        closeBitstream();
        decoder = null;
        fill.clear();
        output.init(input.timeUs, 0);
        return null;
      }
      pcm.flip();
      int bytes = pcm.remaining();
      if (bytes <= 0) {
        output.init(input.timeUs, 0);
        return null;
      }
      output.init(input.timeUs, bytes);
      output.data.put(pcm);
      output.data.flip();
      return null;
    }

    private void closeBitstream() {
      if (bitstream != null) {
        try { bitstream.close(); } catch (Exception ignored) {}
        bitstream = null;
      }
    }

    private static int findSync(byte[] b, int from) {
      for (int i = from; i + 1 < b.length; i++) {
        if ((b[i] & 0xFF) == 0xFF && (b[i + 1] & 0xE0) == 0xE0 && frameLen(b, i) >= 24) return i;
      }
      return -1;
    }

    private static final int[] BR_L2_M1 = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
    private static final int[] BR_L2_M2 = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    private static final int[] SR_M1 = {44100, 48000, 32000};
    private static final int[] SR_M2 = {22050, 24000, 16000};

    private static int frameLen(byte[] b, int i) {
      if (i + 3 >= b.length) return 0;
      int h1 = b[i + 1] & 0xFF;
      int h2 = b[i + 2] & 0xFF;
      int ver = (h1 >> 3) & 3;
      int layer = (h1 >> 1) & 3;
      int brIdx = (h2 >> 4) & 15;
      int srIdx = (h2 >> 2) & 3;
      int pad = (h2 >> 1) & 1;
      if (ver == 1 || layer == 0 || brIdx == 0 || brIdx == 15 || srIdx == 3) return 0;
      int sr = (ver == 3) ? SR_M1[srIdx] : SR_M2[srIdx];
      if (sr <= 0) return 0;
      int[] table = (ver == 3) ? BR_L2_M1 : BR_L2_M2;
      int br = table[brIdx] * 1000;
      if (br <= 0) return 0;
      if (layer == 3) return ((12 * br) / sr + pad) * 4;
      int scale = (ver == 3) ? 144 : 72;
      return scale * br / sr + pad;
    }

    private static byte[] concat(byte[] a, byte[] b) {
      if (a == null || a.length == 0) return b;
      byte[] o = new byte[a.length + b.length];
      System.arraycopy(a, 0, o, 0, a.length);
      System.arraycopy(b, 0, o, a.length, b.length);
      return o;
    }

    private static byte[] slice(byte[] b, int from, int to) {
      if (from < 0) from = 0;
      if (to > b.length) to = b.length;
      if (from >= to) return new byte[0];
      byte[] o = new byte[to - from];
      System.arraycopy(b, from, o, 0, o.length);
      return o;
    }
  }

  static final class FillStream extends InputStream {
    private byte[] buf = new byte[0];
    private int pos;

    void add(byte[] src, int from, int len) {
      int left = buf.length - pos;
      byte[] n = new byte[left + len];
      if (left > 0) System.arraycopy(buf, pos, n, 0, left);
      System.arraycopy(src, from, n, left, len);
      buf = n;
      pos = 0;
    }

    void clear() {
      buf = new byte[0];
      pos = 0;
    }

    @Override
    public int read() {
      if (pos >= buf.length) return -1;
      return buf[pos++] & 0xFF;
    }

    @Override
    public int read(byte[] b, int off, int len) {
      int avail = buf.length - pos;
      if (avail <= 0) return -1;
      int n = Math.min(len, avail);
      System.arraycopy(buf, pos, b, off, n);
      pos += n;
      return n;
    }

    @Override
    public int available() {
      return buf.length - pos;
    }
  }
}
