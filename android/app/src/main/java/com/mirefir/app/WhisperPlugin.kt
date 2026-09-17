package com.mirefir.app

import android.util.Base64
import com.getcapacitor.JSObject
import com.getcapacitor.Plugin
import com.getcapacitor.PluginCall
import com.getcapacitor.PluginMethod
import com.getcapacitor.annotation.CapacitorPlugin
import dev.ffmpegkit.whisper.Whisper
import dev.ffmpegkit.whisper.WhisperConfig
import dev.ffmpegkit.whisper.WhisperModel
import java.io.File
import java.io.FileOutputStream
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

@CapacitorPlugin(name = "Whisper")
class WhisperPlugin : Plugin() {
  private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
  private var model: WhisperModel? = null

  @PluginMethod
  fun ensure(call: PluginCall) {
    scope.launch {
      try {
        ensureReady()
        call.resolve(JSObject().put("ok", true))
      } catch (err: Exception) {
        call.resolve(JSObject().put("ok", false).put("error", err.message ?: "NO_WHISPER"))
      }
    }
  }

  @PluginMethod
  fun transcribe(call: PluginCall) {
    scope.launch {
      try {
        val pcmB64 = call.getString("pcm") ?: ""
        val sampleRate = call.getInt("sampleRate") ?: 16000
        val pcm = Base64.decode(pcmB64, Base64.DEFAULT)
        if (pcm.size < 3200) {
          call.resolve(JSObject().put("ok", false).put("error", "NO_AUDIO"))
          return@launch
        }
        val loaded = ensureReady()
        val wav = File(context.cacheDir, "mirefir-speech.wav")
        writeWav(wav, pcm, sampleRate)
        emit("Распознаю…", 0, 0)
        val result = Whisper.transcribe(loaded, wav.absolutePath, WhisperConfig(language = "auto"))
        val text = result.text?.trim().orEmpty()
        if (text.isEmpty()) {
          call.resolve(
            JSObject()
              .put("ok", false)
              .put("error", "Не услышали. Скажите название канала или «переключи на …»."),
          )
          return@launch
        }
        call.resolve(JSObject().put("ok", true).put("text", text))
      } catch (err: Exception) {
        call.resolve(JSObject().put("ok", false).put("error", err.message ?: "Не удалось распознать голос."))
      }
    }
  }

  @PluginMethod
  fun cancel(call: PluginCall) {
    call.resolve(JSObject().put("ok", true))
  }

  private suspend fun ensureReady(): WhisperModel {
    model?.let { return it }
    val file = modelFile()
    if (!file.exists() || file.length() < MIN_BYTES) {
      downloadModel(file)
    }
    emit("Готовлю голосовую модель…", 0, 0)
    val loaded = Whisper.loadModel(context, file.absolutePath)
    model = loaded
    return loaded
  }

  private fun modelFile(): File {
    val dir = File(context.filesDir, "whisper")
    dir.mkdirs()
    return File(dir, MODEL_NAME)
  }

  private fun downloadModel(dest: File) {
    emit("Скачиваю голосовую модель…", 0, 0)
    val tmp = File(dest.absolutePath + ".part")
    val connection = URL(MODEL_URL).openConnection() as HttpURLConnection
    connection.instanceFollowRedirects = true
    connection.connectTimeout = 20000
    connection.readTimeout = 60000
    connection.setRequestProperty("User-Agent", "MirEfir")
    connection.connect()
    if (connection.responseCode >= 400) {
      throw IllegalStateException("Не удалось скачать голосовую модель (${connection.responseCode})")
    }
    val total = connection.contentLengthLong.coerceAtLeast(0)
    connection.inputStream.use { input ->
      FileOutputStream(tmp).use { output ->
        val buf = ByteArray(64 * 1024)
        var received = 0L
        while (true) {
          val read = input.read(buf)
          if (read <= 0) break
          output.write(buf, 0, read)
          received += read
          emit("Скачиваю голосовую модель…", received, total)
        }
      }
    }
    if (tmp.length() < MIN_BYTES) throw IllegalStateException("Модель голоса скачалась повреждённой")
    if (dest.exists()) dest.delete()
    if (!tmp.renameTo(dest)) {
      tmp.copyTo(dest, overwrite = true)
      tmp.delete()
    }
  }

  private fun emit(text: String, received: Long, total: Long) {
    val data = JSObject()
    data.put("text", text)
    data.put("received", received)
    data.put("total", total)
    notifyListeners("progress", data)
  }

  private fun writeWav(file: File, pcm: ByteArray, sampleRate: Int) {
    val header = ByteArray(44)
    writeAscii(header, 0, "RIFF")
    writeInt(header, 4, 36 + pcm.size)
    writeAscii(header, 8, "WAVE")
    writeAscii(header, 12, "fmt ")
    writeInt(header, 16, 16)
    writeShort(header, 20, 1)
    writeShort(header, 22, 1)
    writeInt(header, 24, sampleRate)
    writeInt(header, 28, sampleRate * 2)
    writeShort(header, 32, 2)
    writeShort(header, 34, 16)
    writeAscii(header, 36, "data")
    writeInt(header, 40, pcm.size)
    FileOutputStream(file).use { out ->
      out.write(header)
      out.write(pcm)
    }
  }

  private fun writeAscii(buf: ByteArray, offset: Int, value: String) {
    value.toByteArray(Charsets.US_ASCII).copyInto(buf, offset)
  }

  private fun writeInt(buf: ByteArray, offset: Int, value: Int) {
    buf[offset] = (value and 0xff).toByte()
    buf[offset + 1] = (value shr 8 and 0xff).toByte()
    buf[offset + 2] = (value shr 16 and 0xff).toByte()
    buf[offset + 3] = (value shr 24 and 0xff).toByte()
  }

  private fun writeShort(buf: ByteArray, offset: Int, value: Int) {
    buf[offset] = (value and 0xff).toByte()
    buf[offset + 1] = (value shr 8 and 0xff).toByte()
  }

  companion object {
    private const val MODEL_NAME = "ggml-base-q5_1.bin"
    private const val MODEL_URL = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base-q5_1.bin"
    private const val MIN_BYTES = 40L * 1024L * 1024L
  }
}
