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

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

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

        new Thread() {
            @Override
            public void run() {
                try {
                    while (true) {
                        sleep(1000);
                        byte[] message = "getValues()".getBytes();
                        DatagramPacket packet = new DatagramPacket(message, message.length, address, port);
                        UDPSocketMaj.send(packet);
                        DatagramPacket packetreponse = null;
                        UDPSocketMaj.receive(packetreponse);
                        DisplayData(packetreponse);
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }

            }
        }.start();

    }

    public void DisplayData(DatagramPacket response)    {
        try {
            final TextView tvValuesFirst = (TextView) findViewById(R.id.tvValueFirst);
            final TextView tvValuesSecond = (TextView) findViewById(R.id.TvValueSecond);
            final TextView tvValueThrid = (TextView) findViewById(R.id.tvValueThrid);
            final Spinner spFirst = (Spinner) findViewById(R.id.spinFirst);
            final Spinner spSecond = (Spinner) findViewById(R.id.spinSecond);
            final Spinner spThird = (Spinner) findViewById(R.id.spinThird);

            tvValuesFirst.setText(spFirst.getSelectedItem().toString());
            tvValuesSecond.setText(spSecond.getSelectedItem().toString());
            tvValueThrid.setText(spThird.getSelectedItem().toString());

        }
        catch (Exception e) {
            e.printStackTrace();
        }

        }

}
