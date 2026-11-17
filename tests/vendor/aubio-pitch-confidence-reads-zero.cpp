// aubio-pitch-confidence-reads-zero: yinfft confidence is 0 on a clean sine
// Links the (patched) aubio directly, no QuickJS, no binding.
// A 440 Hz sine at 44100 Hz has tau ~ 100 > short_period, the branch that
// left peak_pos at 0.

#include <aubio.h>
#include <math.h>
#include <stdio.h>

int
main() {
  int rc = 0;
  const char* methods[] = {"yin", "yinfft"};

  for(const char* m : methods) {
    aubio_pitch_t* p = new_aubio_pitch(m, 4096, 2048, 44100);
    fvec_t* in = new_fvec(2048);
    fvec_t* out = new_fvec(1);

    aubio_pitch_set_unit(p, "Hz");

    for(int k = 0; k < 4; k++) {
      for(int i = 0; i < 2048; i++)
        in->data[i] = 0.8 * sin(2 * M_PI * 440 * (i + k * 2048) / 44100.0);
      aubio_pitch_do(p, in, out);
    }

    float hz = out->data[0], conf = aubio_pitch_get_confidence(p);
    bool ok = fabsf(hz - 440) < 5 && conf > 0.5f;

    printf("%s: %s hz=%.2f confidence=%.3f\n", ok ? "PASS" : "FAIL", m, hz, conf);
    if(!ok)
      rc = 1;

    del_fvec(in);
    del_fvec(out);
    del_aubio_pitch(p);
  }
  return rc;
}
