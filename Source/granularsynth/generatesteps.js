function generate_steps(steps, startstep, endstep, par0, par1) {
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
    }
    // sleep(1000);
    return steps;
}
