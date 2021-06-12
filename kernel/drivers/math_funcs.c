float pow(float num, float exp) {
        float result = num;
        float exp_orig = exp;

        if(exp < 0) exp *= -1; 

        for(int i = 1; i < exp; i++) result *= num;

        if(exp_orig > 0) {
                return result;
        } else if(exp_orig == 0) {
                return 1;
        } else {
                return 1 / result;
        }
}

