#include "evcc_bridge.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    evcc_shell_t s;
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"status\":\"ON\",\"chargingRate\":\"0kw\"}]}",&s));
    assert(s.state=='A'&&!strcmp(s.reason,"ok"));
    assert(shell_vehicle_fresh(&s,100,15099));assert(!evcc_shell_fresh(&s,100,15099));
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"transaction\":{\"statusCar\":\"Charging\"}}]}",&s)&&s.state=='C');
    assert(evcc_shell_fresh(&s,100,15099));assert(!evcc_shell_fresh(&s,100,15100));
    assert(shell_vehicle_fresh(&s,100,15099));assert(!shell_vehicle_fresh(&s,100,15100));
    assert(!evcc_shell_fresh(&s,100,99));assert(!evcc_shell_fresh(&s,0,100));
    const char *states[]={"SuspendedEV","SuspendedEVSE","Faulted","Available","Preparing","Finishing","ON",""};
    const char expected[]={'B','B','F','A','B','D','?','?'};
    for(unsigned i=0;i<8;i++) {
        char json[200];snprintf(json,sizeof(json),"{\"connectors\":[{\"id\":1,\"transaction\":{\"statusCar\":\"%s\"}}]}",states[i]);
        assert(evcc_shell_parse(json,&s));assert(s.state==expected[i]);
    }
    assert(!evcc_shell_parse("{\"connectors\":[{\"id\":1},{\"id\":1}]}",&s));
    assert(!evcc_shell_parse("{\"connectors\":[],\"connectors\":[{\"id\":1}]}",&s));
    assert(!evcc_shell_parse("{\"connectors\":[{\"id\":1,\"transaction\":{\"statusCar\":\"Charging\",\"statusCar\":\"Faulted\"}}]}",&s));
    assert(!evcc_shell_parse("{broken}",&s));assert(s.state=='?');
    assert(!evcc_shell_parse("{\"connectors\":[{\"id\":2}]}",&s));
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"transaction\":null}]}",&s)&&s.state=='A');
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"status\":\"Preparing\"}]}",&s)&&s.state=='B');
    assert(!evcc_shell_fresh(&s,100,15099));
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"status\":\"Finishing\"}]}",&s)&&s.state=='D');
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"status\":\"Faulted\"}]}",&s)&&s.state=='F');
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"transaction\":{\"status\":\"CHARGING\",\"statusCar\":\"idle\"}}]}",&s)&&s.state=='B');
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"transaction\":{\"status\":\"FINISHED\",\"statusCar\":\"idle\"}}]}",&s)&&s.state=='D');
    assert(evcc_shell_parse("{\"connectors\":[{\"id\":1,\"transaction\":{\"statusCar\":\"idle\"}}]}",&s)&&s.state=='?');
    evcc_control_t c;evcc_configure(&c,false);
    assert(!evcc_set_current(&c,8,8,16,1));assert(!evcc_set_enabled(&c,true,true,1));
    evcc_configure(&c,true);
    assert(!evcc_set_enabled(&c,true,true,100));
    assert(!evcc_set_current(&c,7.9f,8,16,100));assert(!evcc_set_current(&c,16.1f,8,16,100));
    assert(!evcc_set_current(&c,NAN,8,16,100));assert(!evcc_set_current(&c,8,NAN,16,100));
    assert(evcc_set_current(&c,8,8,16,100));assert(!c.enabled);
    assert(!evcc_set_enabled(&c,true,false,100));
    assert(evcc_set_enabled(&c,true,true,100));assert(!evcc_tick(&c,90099));
    assert(evcc_tick(&c,90100)&&c.expired&&!c.enabled);
    evcc_touch(&c,90101);assert(!c.enabled); /* Polling cannot restart an expired lease. */
    assert(evcc_set_enabled(&c,true,true,90200));
    evcc_touch(&c,180200);assert(!c.enabled&&c.expired);
    assert(evcc_set_enabled(&c,true,true,180201));
    evcc_local_stop(&c);assert(!evcc_set_enabled(&c,true,true,90300));
    assert(evcc_set_current(&c,10,8,16,90300));assert(!c.enabled&&c.inhibited);
    assert(evcc_set_enabled(&c,false,false,90400));assert(c.inhibited);
    evcc_configure(&c,true);assert(!c.enabled&&!c.current_set&&!c.inhibited);
    assert(!evcc_set_phases(&c,1,false,true,100));
    assert(!evcc_set_phases(&c,2,true,true,100));
    assert(!evcc_set_phases(&c,1,true,false,100));
    assert(evcc_set_phases(&c,1,true,true,100));
    assert(c.preparing && !c.enabled && evcc_desired_phases(&c,3)==1);
    assert(evcc_set_current(&c,9,8.7f,16,200));
    assert(c.preparing && !c.enabled); /* A current must not start prepared charging. */
    assert(evcc_set_enabled(&c,true,true,300));
    assert(!c.preparing && evcc_desired_phases(&c,3)==1);
    assert(evcc_set_phases(&c,3,true,true,400));
    assert(c.enabled && evcc_desired_phases(&c,1)==3);
    assert(evcc_set_enabled(&c,false,false,500));
    assert(!c.preparing && evcc_desired_phases(&c,1)==3);
    assert(evcc_set_phases(&c,1,true,true,600));
    evcc_local_stop(&c);
    assert(evcc_desired_phases(&c,1)==3);
    assert(!evcc_set_phases(&c,1,true,true,700));
    evcc_configure(&c,true);
    assert(evcc_set_phases(&c,1,true,true,100));
    assert(evcc_tick(&c,90100) && !c.preparing && evcc_desired_phases(&c,1)==3);
    evcc_touch(&c,90101);assert(!c.preparing && !c.enabled);
    evcc_configure(&c,true);assert(!c.phase_set && !c.preparing);
    assert(evcc_emergency_stop_json("{\"enabled\":false,\"mode\":\"off\"}"));
    assert(!evcc_emergency_stop_json("{\"enabled\":false,\"mode\":\"manual\"}"));
    assert(!evcc_emergency_stop_json("{\"enabled\":false,\"current_a\":12}"));
    assert(!evcc_emergency_stop_json("{\"enabled\":false,\"enabled\":true}"));
    assert(!evcc_emergency_stop_json("{}"));
    assert(evcc_host_valid("192.168.1.65"));assert(evcc_host_valid("shell.local"));
    assert(!evcc_host_valid("host/path"));assert(!evcc_host_valid("user@host"));assert(!evcc_host_valid("host:12800"));
    evcc_configure(&c,true);assert(evcc_set_current(&c,9,8.7,16,90));assert(evcc_set_enabled(&c,true,true,100));evcc_local_stop(&c);
    assert(!evcc_set_enabled(&c,true,true,200));assert(evcc_local_release(&c,300));
    assert(!c.enabled && !c.preparing && !c.inhibited); /* Unlock is never a start. */
    assert(!evcc_local_release(&c,301));assert(evcc_set_enabled(&c,true,true,400));
    evcc_configure(&c,false);assert(!evcc_local_release(&c,500));
    puts("evcc parser, status freshness, current bounds, lease and stop ownership: PASS");
}
