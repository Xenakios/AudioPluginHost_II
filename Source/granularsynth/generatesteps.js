// 2 3.86 950

function generate_steps(steps, startstep, endstep, params) {
    x0 = 0.4;
    //for (var i = 0; i < par2; i++) {
    //    x1 = par1 * x0 * (1.0 - x0);
    //    x0 = x1;
    //}
    for (var i = startstep; i < endstep; i++) {
        // steps[i] = -1.0 + 2.0 * Math.random();
        if (params[0] == 0)
            steps[i] = params[1] * Math.cos(2 * 3.141592653 / (endstep - startstep) * (i - startstep))
        if (params[0] == 1) {
            if (Math.random() < params[1]) {
                steps[i] = 1.0;
            } else {
                steps[i] = -1.0;
            }
        }
        if (params[0] == 2) {
            x1 = params[1] * x0 * (1.0 - x0);
            steps[i] = -1.0 + 2.0 * x0;
            x0 = x1;
        }
        if (params[0] == 3) {
            partial = (i - startstep) + 1;
            if (partial > 0 && partial < 17)
                steps[i] = Math.log2(partial) / 4.0;
        }
        if (params[0] == 4) {
            if (i % 13 == 0 || i % 7 == 2)
                steps[i] = 1.0;
            else steps[i] = 0.0;
        }
    }
    // sleep(1000);
    return steps;
}
