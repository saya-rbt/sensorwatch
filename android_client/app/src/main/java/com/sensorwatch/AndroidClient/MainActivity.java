package com.sensorwatch.AndroidClient;

import android.os.Bundle;
import android.util.Range;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.AdapterView;

import androidx.appcompat.app.AppCompatActivity;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketException;
import java.util.ArrayList;

public class MainActivity extends AppCompatActivity {

    private DatagramSocket UDPSocket;
    private DatagramSocket UDPSocketMaj;
    private InetAddress address;
    private Spinner spFirst;
    private Spinner spSecond;
    private Spinner spThird;
    private Spinner[] spinners;
    private Boolean triggerFlag = false;
    private TextView tvValuesFirst;
    private TextView tvValuesSecond;
    private TextView tvValueThrid;
    private String Strtemp;
    private String Strlumi;
    private String StrHumi;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        this.tvValuesFirst = (TextView) findViewById(R.id.tvValueFirst);
        this.tvValuesSecond = (TextView) findViewById(R.id.TvValueSecond);
        this.tvValueThrid = (TextView) findViewById(R.id.tvValueThrid);

        spFirst = (Spinner) findViewById(R.id.spinFirst);
        spSecond = (Spinner) findViewById(R.id.spinSecond);
        spThird = (Spinner) findViewById(R.id.spinThird);
        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(this,
                R.array.planets_array, android.R.layout.simple_spinner_item);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        spFirst.setAdapter(adapter);
        spSecond.setAdapter(adapter);
        spThird.setAdapter(adapter);
        spFirst.setSelection(0);
        spSecond.setSelection(1);
        spThird.setSelection(2);

        spinners = new Spinner[] {spFirst, spSecond, spThird};

        findViewById(R.id.btSendMessage).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                int nbrepet = 1;
                String data = spFirst.getSelectedItem().toString().substring(0, 1) + spSecond.getSelectedItem().toString().substring(0, 1) + spThird.getSelectedItem().toString().substring(0, 1);
                int port = Integer.valueOf(((TextView) findViewById(R.id.EdPort)).getText().toString());
                String address = ((TextView) findViewById(R.id.EdIpServeur)).getText().toString();
                SendData(nbrepet, data, port, address);
            }
        });

        findViewById(R.id.btReceiveData).setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                int port = Integer.valueOf(((TextView) findViewById(R.id.EdPort)).getText().toString());
                ScanData(port);
            }
        });
    }

    @Override
    protected void onStart(){
        super.onStart();
        AdapterView.OnItemSelectedListener selectListener = new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parentView, View selectedItemView, int position, long id) {
                if(triggerFlag){
                    triggerFlag = false;
                    return;
                }
                Spinner toSwap = null;
                int swapValue = 0;
                ArrayList<Integer> myList = new ArrayList<>();
                myList.add(0);
                myList.add(1);
                myList.add(2);

                myList.remove(myList.indexOf(position));
                for(Spinner item : spinners){
                    if(parentView.getId() != item.getId()){
                        if(item.getSelectedItemPosition() == position){
                            toSwap = item;
                        }
                        else{
                            myList.remove(myList.indexOf(item.getSelectedItemPosition()));
                        }
                    }
                }
                if(toSwap != null){
                    triggerFlag = true;
                    toSwap.setSelection(myList.get(0));
                }

            }

            @Override
            public void onNothingSelected(AdapterView<?> parentView) {
                // your code here
            }
        };

        spFirst.setOnItemSelectedListener(selectListener);
        spSecond.setOnItemSelectedListener(selectListener);
        spThird.setOnItemSelectedListener(selectListener);
    }

    /// Initialise une socket avec les parametres recupere dans l'interface graphique pour l'envoi des données
    public void Initreseau(InetAddress address) {
        try {
            this.UDPSocket = new DatagramSocket();
            this.address = address;
            this.UDPSocketMaj = new DatagramSocket();
        } catch (SocketException e) {
            e.printStackTrace();
        }
    }

    /// Envoi les données dans la socket defini par la methode InitReseau
    public void SendInstruction(final byte[] data, final int port) {
        new Thread() {
            @Override
            public void run() {
                try {
                    DatagramPacket packet = new DatagramPacket(data, data.length, address, port);
                    UDPSocket.send(packet);
                    DatagramPacket packetreponse = null;
                } catch (Exception e) {
                    e.printStackTrace();
                }

            }
        }.start();
    }

    /// Envoi X fois la data
    public void SendData(final int nbRepet, final String Sdata, final int port, final String address) {
        new Thread() {
            @Override
            public void run() {
                try {
                    Initreseau(InetAddress.getByName(address));
                    for (int i = 0; i < nbRepet; i++) {
                        byte[] data = Sdata.getBytes();
                        SendInstruction(data, port);
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }.start();

    }

    // Modifie l affichage en fonction de la tram recu
    public void ScanData(final int port) {

        Thread test = new Thread() {
            @Override
            public void run() {
                try {
                        byte[] message = "getValues()".getBytes();
                        DatagramPacket packet = new DatagramPacket(message, message.length, address, port);
                        UDPSocketMaj.send(packet);
                        final int MTU = 1500;
                        byte[] data;
                        data = new byte[MTU];
                        DatagramPacket packetreponse= new DatagramPacket(data, MTU);
                        UDPSocketMaj.receive(packetreponse);
                        String pain = new String(packetreponse.getData());
                        String[] answers = pain.split(";");
                        Strtemp = answers[0];
                        Strlumi = answers[1];
                        StrHumi = answers[2];

                } catch (Exception e) {
                    e.printStackTrace();
                }

            }
        };
        test.start();
        try {
            test.join();
        } catch (InterruptedException e) {
            e.printStackTrace();
        }

        DisplayData(Strtemp,Strlumi,StrHumi);

    }

    public void DisplayData(String Strtemp , String Strlumi , String StrHumi)    {
        try {

            this.tvValuesFirst.setText(this.spFirst.getSelectedItem().toString());
            this.tvValuesSecond.setText(this.spSecond.getSelectedItem().toString());
            this.tvValueThrid.setText(this.spThird.getSelectedItem().toString());

            switch (this.tvValuesFirst.getText().toString())
            {
                case "Temperature" :
                    this.tvValuesFirst.setText(this.spFirst.getSelectedItem().toString()+" : "+Strtemp);
                    break;
                case "Humidity" :
                    this.tvValuesFirst.setText(this.spFirst.getSelectedItem().toString()+" : "+StrHumi);
                    break;
                case "Luminosity" :
                    this.tvValuesFirst.setText(this.spFirst.getSelectedItem().toString()+" : "+Strlumi);
                    break;
                default:
                    break;
            }

            switch (this.tvValuesSecond.getText().toString())
            {
                case "Temperature" :
                    this.tvValuesSecond.setText(this.spSecond.getSelectedItem().toString()+" : "+Strtemp);
                    break;
                case "Humidity" :
                    this.tvValuesSecond.setText(this.spSecond.getSelectedItem().toString()+" : "+StrHumi);
                    break;
                case "Luminosity" :
                    this.tvValuesSecond.setText(this.spSecond.getSelectedItem().toString()+" : "+Strlumi);
                    break;
                default:
                    break;
            }

            switch (this.tvValueThrid.getText().toString())
            {
                case "Temperature" :
                    this.tvValueThrid.setText(this.spThird.getSelectedItem().toString()+" : "+Strtemp);
                    break;
                case "Humidity" :
                    this.tvValueThrid.setText(this.spThird.getSelectedItem().toString()+" : "+StrHumi);
                    break;
                case "Luminosity" :
                    this.tvValueThrid.setText(this.spThird.getSelectedItem().toString()+" : "+Strlumi);
                    break;
                default:
                    break;
            }


        }
        catch (Exception e) {
            e.printStackTrace();
        }

        }

}
