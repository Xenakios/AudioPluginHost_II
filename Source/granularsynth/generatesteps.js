// 2 3.86 950

function generate_steps(steps, startstep, endstep, par0, par1, par2, par3) {
    x0 = 0.4;
    for (var i = 0; i < par2; i++) {
        x1 = par1 * x0 * (1.0 - x0);
        x0 = x1;
    }
    for (var i = startstep; i < endstep; i++) {
        // steps[i] = -1.0 + 2.0 * Math.random();
        if (par0 == 0)
            steps[i] = par1 * Math.cos(2 * 3.141592653 / (endstep - startstep) * (i - startstep))
        if (par0 == 1) {
            if (Math.random() < par1) {
                steps[i] = 1.0;
            } else {
                steps[i] = -1.0;
            }
        }
        if (par0 == 2) {
            x1 = par1 * x0 * (1.0 - x0);
            steps[i] = -1.0 + 2.0 * x0;
            x0 = x1;
        }
        if (par0 == 3) {
            partial = (i - startstep) + 1;
            if (partial > 0 && partial < 17)
                steps[i] = Math.log2(partial) / 4.0;
        }
    }
    // sleep(1000);
    return steps;
}
