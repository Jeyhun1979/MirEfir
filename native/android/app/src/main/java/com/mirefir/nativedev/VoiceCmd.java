package com.mirefir.nativedev;

import android.util.Log;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

/**
 * Разбор голосовой команды как на Windows (voice.c): «переключи/включи (на) …», сопоставление с каналом
 * (fold_text, score_ch, pick_channel) и грамматика Vosk с командными префиксами (build_grammar).
 */
final class VoiceCmd {
  static final int GRAMMAR_LIMIT = 4000; /* Windows 20000; на приставке грамматика такого размера компилируется минутами */
  private static final int MAX_PHRASE = 72;

  static final class Ch {
    int index;
    boolean fav;
    String name;
    String tvg;
  }

  /** Результат разбора фразы: sw — была команда «переключи», query — что искать. */
  static final class Cmd {
    boolean sw;
    String query = "";
  }

  private static final String[] ZAP = {
      "переключай на ", "переключить на ", "переключи на ", "включай на ", "включить на ", "включи на ",
      "переключай ", "переключить ", "переключи ", "включай ", "включить ", "включи ",
      "открой канал ", "открой ", "поставь ", "switch to ", "turn on ", "put on "};

  private static final String[] FIND = {"найди ", "найти ", "поиск ", "ищи ", "find ", "search "};

  private static final Set<String> DROP = new HashSet<>();

  static {
    for (String s : new String[] {"uhd", "fhd", "hd", "sd", "4k", "hevc", "hdr", "tv", "тв", "канал", "телеканал", "программа"})
      DROP.add(s);
  }

  private VoiceCmd() {}

  /** Нижний регистр, ё→е, только буквы/цифры, без «HD/тв/канал…», «один/два/три» → 1/2/3. */
  static String fold(String in) {
    if (in == null) return "";
    StringBuilder t = new StringBuilder();
    boolean space = false;
    String low = in.toLowerCase(Locale.ROOT).replace('ё', 'е');
    for (int i = 0; i < low.length() && t.length() < 254; i++) {
      char c = low.charAt(i);
      boolean let = (c >= 'a' && c <= 'z') || (c >= 'а' && c <= 'я') || (c >= '0' && c <= '9');
      if (let) {
        t.append(c);
        space = false;
      } else if (!space && t.length() > 0) {
        t.append(' ');
        space = true;
      }
    }
    StringBuilder out = new StringBuilder();
    for (String w : t.toString().trim().split(" +")) {
      if (w.isEmpty() || DROP.contains(w)) continue;
      if (w.equals("один") || w.equals("одна")) w = "1";
      else if (w.equals("два")) w = "2";
      else if (w.equals("три")) w = "3";
      if (out.length() > 0) out.append(' ');
      out.append(w);
    }
    return out.toString();
  }

  private static int editDist(String a, String b) {
    if (a.equals(b)) return 0;
    int na = a.length(), nb = b.length();
    if (na == 0 || nb == 0) return 99;
    if (na > 48 || nb > 48 || Math.abs(na - nb) > 3) return 99;
    int[] prev = new int[nb + 1], cur = new int[nb + 1];
    for (int j = 0; j <= nb; j++) prev[j] = j;
    for (int i = 1; i <= na; i++) {
      cur[0] = i;
      for (int j = 1; j <= nb; j++) {
        int cost = a.charAt(i - 1) == b.charAt(j - 1) ? 0 : 1;
        int v = prev[j] + 1;
        if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
        if (prev[j - 1] + cost < v) v = prev[j - 1] + cost;
        cur[j] = v;
      }
      int[] sw = prev;
      prev = cur;
      cur = sw;
    }
    return prev[nb];
  }

  private static int score(Ch c, String query, String[] parts) {
    int best = 0;
    for (int i = 0; i < 2; i++) {
      String name = fold(i == 0 ? c.name : c.tvg);
      if (name.isEmpty()) continue;
      int s = 0;
      if (name.equals(query)) s = 120 - name.length();
      else if (name.startsWith(query) || (name.length() >= 3 && query.startsWith(name))) s = 86;
      else if (name.contains(query) || (name.length() >= 3 && query.contains(name))) s = 72;
      else if (parts.length >= 2) {
        boolean all = true;
        for (String p : parts) {
          if (!name.contains(p)) {
            all = false;
            break;
          }
        }
        if (all) s = 70;
      }
      if (s == 0 && name.length() >= 4 && query.length() >= 4 && editDist(name, query) <= 1) s = 68;
      else if (s == 0 && name.length() >= 5 && query.length() >= 5 && editDist(name, query) <= 2) s = 62;
      if (s > best) best = s;
    }
    return best;
  }

  /** Индекс канала в плейлисте или -1. Избранные проверяются первыми, как на Windows. */
  static int pick(List<Ch> list, String spoken) {
    String query = fold(spoken);
    if (query.isEmpty()) return -1;
    List<String> pr = new ArrayList<>();
    for (String p : query.split(" +")) if (p.length() >= 2 && pr.size() < 8) pr.add(p);
    String[] parts = pr.toArray(new String[0]);
    int best = -1, bestS = 0;
    for (int pass = 0; pass < 2; pass++) {
      for (Ch c : list) {
        if ((pass == 0 && !c.fav) || (pass == 1 && c.fav)) continue;
        int s = score(c, query, parts);
        if (s > bestS) {
          bestS = s;
          best = c.index;
        }
      }
      if (bestS >= 60) return best;
    }
    return bestS >= 60 ? best : -1;
  }

  private static boolean starts(String s, String p) {
    return s.regionMatches(true, 0, p, 0, p.length());
  }

  /** «переключи на Первый» → sw=true, query="Первый"; «найди канал Матч» → query="Матч". */
  static Cmd parse(String raw) {
    Cmd r = new Cmd();
    String t = raw == null ? "" : raw.trim().replaceAll(" +", " ");
    String p = t;
    for (String z : ZAP) {
      if (!starts(p, z)) continue;
      r.sw = true;
      p = p.substring(z.length()).trim();
      if (starts(p, "на ") || starts(p, "on ")) p = p.substring(3);
      break;
    }
    if (!r.sw) {
      for (String f : FIND) {
        if (!starts(p, f)) continue;
        p = p.substring(f.length()).trim();
        if (starts(p, "канал ")) p = p.substring(6);
        break;
      }
    }
    /* голое «переключи на» без названия: команды нет, искать нечего */
    r.query = p.isEmpty() ? t : p.trim();
    return r;
  }

  private static String cleanPhrase(String in) {
    if (in == null) return "";
    StringBuilder b = new StringBuilder();
    boolean space = false;
    String low = in.toLowerCase(Locale.ROOT).replace('ё', 'е');
    for (int i = 0; i < low.length() && b.length() < 90; i++) {
      char c = low.charAt(i);
      boolean let = (c >= 'a' && c <= 'z') || (c >= 'а' && c <= 'я') || (c >= '0' && c <= '9');
      if (let) {
        b.append(c);
        space = false;
      } else if (!space && b.length() > 0) {
        b.append(' ');
        space = true;
      }
    }
    return b.toString().trim();
  }

  private static String stripTail(String s) {
    String[] tails = {" uhd", " fhd", " hd", " sd", " 4k", " hevc", " hdr"};
    if (s.length() <= 3) return s;
    for (String t : tails) {
      if (s.length() > t.length() && s.endsWith(t)) return s.substring(0, s.length() - t.length());
    }
    return s;
  }

  /**
   * Грамматика Vosk: имена каналов и те же имена с «переключи на / включи …» (JSON-массив фраз + «[unk]»).
   * Избранные идут первыми, чтобы при лимите фраз они не отрезались.
   */
  static String grammar(List<Ch> list, boolean en) {
    if (list.isEmpty()) return null;
    List<String> phrases = new ArrayList<>();
    Set<String> seen = new HashSet<>();
    for (int pass = 0; pass < 2; pass++) {
      for (Ch c : list) {
        if ((pass == 0 && !c.fav) || (pass == 1 && c.fav)) continue;
        for (String raw : new String[] {c.name, c.tvg}) {
          String p = stripTail(cleanPhrase(raw));
          if (p.length() < 2 || p.length() > MAX_PHRASE) continue;
          if (seen.add(p)) phrases.add(p);
        }
      }
    }
    String[] pref = en ? new String[] {"", "switch to ", "turn on "}
        : new String[] {"", "переключи на ", "переключай на ", "включи ", "включай "};
    StringBuilder b = new StringBuilder("[");
    int count = 0;
    /* первые имена (избранные) — со всеми префиксами, остальные — голым именем; всё в пределах лимита фраз.
       Каналы, не попавшие в грамматику, находятся вторым проходом без грамматики (Voice.voskLoop). */
    int names = Math.min(phrases.size(), GRAMMAR_LIMIT * 2 / 5);
    int full = Math.max(1, Math.min(names, (GRAMMAR_LIMIT - names) / Math.max(1, pref.length - 1)));
    for (int i = 0; i < names; i++) {
      for (int k = 0; k < pref.length; k++) {
        if (i >= full && k > 0) break;
        if (count > 0) b.append(',');
        b.append('"').append(pref[k]).append(phrases.get(i)).append('"');
        count++;
      }
    }
    if (count > GRAMMAR_LIMIT) Log.w("MirEfirVoice", "grammar over limit: " + count);
    if (count == 0) return null;
    b.append(",\"[unk]\"]");
    return b.toString();
  }
}
