        control_report(&settings,actual_a,request,house_power_w,house_ok,currents);
        bool private_smoothed=false;
#if CONFIG_TEE_HTTP_CONTROL_TRIAL
        bool private_reply_current=false;
        unsigned private_reply_phases=settings.charge_phases;
        float private_reply_neutral=settings.grid_limit_a+shell_http_trial.normal_offset_a;
        float private_reply_stop=settings.grid_limit_a+settings.max_charge_a+1;
        int64_t private_reply_trigger=0;
        if(shell_http_trial.armed){
            bool current_reply=req[0]==settings.wallbox_address&&req[1]==3&&req[2]==0x50&&req[3]==0x0c&&req[4]==0&&req[5]==6;
            shell_http_trial_report(&shell_http_trial,&evcc_shell.http_control,evcc_shell_at,report_now,
                request,settings.charge_phases,settings.grid_limit_a,settings.min_charge_a,settings.max_charge_a,current_reply,currents);
            if(settings.charge_phases==1||settings.charge_phases==3){
                shell_http_trial_smooth(&shell_http_trial,&manual_filter,&settings,request,report_now,current_reply,currents);
                private_smoothed=true;
                private_reply_current=current_reply;private_reply_trigger=shell_http_trial.coast_trigger_at;
            }
        }
#endif
        bool http_reply_current=settings.shell_http_control_enabled&&http_regulator.running&&
            req[0]==settings.wallbox_address&&req[1]==3&&req[2]==0x50&&req[3]==0x0c&&req[4]==0&&req[5]==6;
        int64_t http_reply_trigger=0;
        if(settings.shell_http_control_enabled&&http_regulator.running){
            shell_http_trial_report(&http_regulator.control,&evcc_shell.http_control,evcc_shell_at,report_now,
                request,settings.charge_phases,settings.grid_limit_a,settings.min_charge_a,settings.max_charge_a,http_reply_current,currents);
            shell_http_trial_smooth(&http_regulator.control,&manual_filter,&settings,request,report_now,http_reply_current,currents);
            http_reply_trigger=http_regulator.control.coast_trigger_at;private_smoothed=true;
        }
        if(!private_smoothed)manual_report_smooth(&manual_filter,&settings,request,report_now,currents);
